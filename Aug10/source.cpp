/**
 * \file
 * Contains base class TParticleSource and several dervied particle source classes.
 * Class TSource creates one of these according to user input.
 */

#include "source.h"
#include "neutron.h"
#include "proton.h"
#include "electron.h"
#include "geometry.h"
#include "mc.h"
#include "trianglemesh.h"
#include "globals.h"

static const int MAX_DICE_ROLL = 42000000; ///< number of tries to find particle start point

TParticleSource::TParticleSource(const string ParticleName, double ActiveTime): fActiveTime(ActiveTime), fParticleName(ParticleName), ParticleCounter(0){

}

TParticleSource::~TParticleSource(){

}


TParticle* TParticleSource::CreateParticle(TMCGenerator &mc, double t, double x, double y, double z, double E, double phi, double theta, int polarisation, TGeometry &geometry, TFieldManager *field){
	TParticle *p;
	if (fParticleName == NAME_NEUTRON)
		p = new TNeutron(++ParticleCounter, t, x, y, z, E, phi, theta, mc, geometry, field);
	else if (fParticleName == NAME_PROTON)
		p = new TProton(++ParticleCounter, t, x, y, z, E, phi, theta, mc, geometry, field);
	else if (fParticleName == NAME_ELECTRON)
		p = new TElectron(++ParticleCounter, t, x, y, z, E, phi, theta, mc, geometry, field);
	else{
		cout << "Could not create particle " << fParticleName << '\n';
		exit(-1);
	}
	return p;
}


TSurfaceSource::TSurfaceSource(const string ParticleName, double ActiveTime, double E_normal): TParticleSource(ParticleName, ActiveTime), sourcearea(0), Enormal(E_normal){

}


TParticle* TSurfaceSource::CreateParticle(TMCGenerator &mc, TGeometry &geometry, TFieldManager *field){
	double t = mc.UniformDist(0, fActiveTime);
	double RandA = mc.UniformDist(0,sourcearea);
	double SumA = 0;
	vector<TTriangle>::iterator i;
	for (i = sourcetris.begin(); i != sourcetris.end(); i++){
		SumA += i->area();
		if (RandA <= SumA) break;
	}
	double a = mc.UniformDist(0,1); // generate random point on triangle (see Numerical Recipes 3rd ed., p. 1114)
	double b = mc.UniformDist(0,1);
	if (a+b > 1){
		a = 1 - a;
		b = 1 - b;
	}
	CVector nv = i->normal();
	CPoint p = i->tri[0] + a*(i->tri[1] - i->tri[0]) + b*(i->tri[2] - i->tri[0]) + nv*REFLECT_TOLERANCE;

	double Ekin = mc.Spectrum(fParticleName);
	double phi_v = mc.UniformDist(0, 2*pi); // generate random velocity angles in upper hemisphere
	double theta_v = mc.SinCosDist(0, 0.5*pi); // Lambert's law!
	if (Enormal > 0){
		double vnormal = sqrt(Ekin*cos(theta_v)*cos(theta_v) + Enormal); // add E_normal to component normal to surface
		double vtangential = sqrt(Ekin)*sin(theta_v);
		theta_v = atan2(vtangential, vnormal); // update angle
		Ekin = vnormal*vnormal + vtangential*vtangential; // update energy
	}

	double v[3] = {cos(phi_v)*sin(theta_v), sin(phi_v)*sin(theta_v), cos(theta_v)};
	double n[3] = {nv[0], nv[1], nv[2]};
	RotateVector(v, n);
	phi_v = atan2(v[1],v[0]);
	theta_v = acos(v[2]);
	int polarisation = mc.DicePolarisation(fParticleName);

	return TParticleSource::CreateParticle(mc, t, p[0], p[1], p[2], Ekin, phi_v, theta_v, polarisation, geometry, field);
}


TVolumeSource::TVolumeSource(std::string ParticleName, double ActiveTime, bool PhaseSpaceWeighting)
			:TParticleSource(ParticleName, ActiveTime), fPhaseSpaceWeighting(PhaseSpaceWeighting){

}


TParticle* TVolumeSource::CreateParticle(TMCGenerator &mc, TGeometry &geometry, TFieldManager *field){
	double t = mc.UniformDist(0, fActiveTime);
	double E = mc.Spectrum(fParticleName);
	double phi_v, theta_v;
	mc.AngularDist(fParticleName, phi_v, theta_v);
	int polarisation = mc.DicePolarisation(fParticleName);
	double x, y, z;
	RandomPointInSourceVolume(mc, x, y, z);
	TParticle *p;
	if (fPhaseSpaceWeighting){
		double H = E; // if spatial distribution should be weighted by available phase space the energy spectrum N(E) determines the total energy H
		cout << "Trying to find starting position for " << fParticleName << " with total energy = " << H*1e9 << " neV ";
		for (int nroll = 0; nroll <= MAX_DICE_ROLL; nroll++){
			if (nroll % 100000 == 0){
				cout << '.'; // print progress
			}
			p = TParticleSource::CreateParticle(mc, t, x, y, z, H, phi_v, theta_v, polarisation, geometry, field);
			ParticleCounter--;
			double V = p->Hstart() - H; // a particle is created with Ekin = H, so the total energy of particle is actually H + V
			if (mc.UniformDist(0,1) < sqrt((H - V)/H)){ // weight density with sqrt(Ekin/H)
				E = H - V; // calculate correct kinetic energy Ekin = H - V
				delete p;
				if (E > 0)
					break;
			}
			else if (nroll >= MAX_DICE_ROLL){
				p->ID = ID_INITIAL_NOT_FOUND;
				printf("\nABORT: Failed %i times to find a compatible spot!! NO particle will be simulated!!\n\n", MAX_DICE_ROLL);
				return p;
			}
			RandomPointInSourceVolume(mc, x, y, z);
		}
	}
	cout << '\n';
	return TParticleSource::CreateParticle(mc, t, x, y, z, E, phi_v, theta_v, polarisation, geometry, field);
}



TCuboidVolumeSource::TCuboidVolumeSource(const string ParticleName, double ActiveTime, bool PhaseSpaceWeighting, double x_min, double x_max, double y_min, double y_max, double z_min, double z_max)
	: TVolumeSource(ParticleName, ActiveTime, PhaseSpaceWeighting), xmin(x_min), xmax(x_max), ymin(y_min), ymax(y_max), zmin(z_min), zmax(z_max){

}


void TCuboidVolumeSource::RandomPointInSourceVolume(TMCGenerator &mc, double &x, double &y, double &z){
	x = mc.UniformDist(xmin, xmax);
	y = mc.UniformDist(ymin, ymax);
	z = mc.UniformDist(zmin, zmax);
}



TCylindricalVolumeSource::TCylindricalVolumeSource(const string ParticleName, double ActiveTime, bool PhaseSpaceWeighting, double r_min, double r_max, double phi_min, double phi_max, double z_min, double z_max)
	: TVolumeSource(ParticleName, ActiveTime, PhaseSpaceWeighting), rmin(r_min), rmax(r_max), phimin(phi_min), phimax(phi_max), zmin(z_min), zmax(z_max){

}


void TCylindricalVolumeSource::RandomPointInSourceVolume(TMCGenerator &mc, double &x, double &y, double &z){
	double r = mc.LinearDist(rmin, rmax); // weighting because of the volume element and a r^2 probability outwards
	double phi_r = mc.UniformDist(phimin,phimax);
	x = r*cos(phi_r);
	y = r*sin(phi_r);
	z = mc.UniformDist(zmin,zmax);
}


bool TCylindricalSurfaceSource::InSourceVolume(CPoint p){
	double r = sqrt(p[0]*p[0] + p[1]*p[1]);
	double phi = atan2(p[1],p[0]);
	return (r >= rmin && r <= rmax &&
			phi >= phimin && phi <= phimax &&
			p[2] >= zmin && p[2] <= zmax); // check if point is in custom paramter range
}


TCylindricalSurfaceSource::TCylindricalSurfaceSource(const string ParticleName, double ActiveTime, TGeometry &geometry, double E_normal, double r_min, double r_max, double phi_min, double phi_max, double z_min, double z_max)
	: TSurfaceSource(ParticleName, ActiveTime, E_normal), rmin(r_min), rmax(r_max), phimin(phi_min), phimax(phi_max), zmin(z_min), zmax(z_max){
	for (CIterator i = geometry.mesh.triangles.begin(); i != geometry.mesh.triangles.end(); i++){
		if (InSourceVolume(i->tri[0]) && InSourceVolume(i->tri[1]) && InSourceVolume(i->tri[2])){
			sourcetris.push_back(*i);
			sourcearea += i->area();
		}
	}
	printf("Source Area: %g m^2\n",sourcearea);
}


TSTLVolumeSource::TSTLVolumeSource(const string ParticleName, double ActiveTime, bool PhaseSpaceWeighting, string sourcefile): TVolumeSource(ParticleName, ActiveTime, PhaseSpaceWeighting){
	kdtree.ReadFile(sourcefile.c_str(),0);
	kdtree.Init();
}


void TSTLVolumeSource::RandomPointInSourceVolume(TMCGenerator &mc, double &x, double &y, double &z){
	double p[3];
	for(;;){
		p[0] = mc.UniformDist(kdtree.tree.bbox().xmin(),kdtree.tree.bbox().xmax()); // random point
		p[1] = mc.UniformDist(kdtree.tree.bbox().ymin(),kdtree.tree.bbox().ymax()); // random point
		p[2] = mc.UniformDist(kdtree.tree.bbox().zmin(),kdtree.tree.bbox().zmax()); // random point
		if (kdtree.InSolid(p)){
			x = p[0];
			y = p[1];
			z = p[2];
			break;
		}
	}
}


TSTLSurfaceSource::TSTLSurfaceSource(const string ParticleName, double ActiveTime, TGeometry &geometry, string sourcefile, double E_normal): TSurfaceSource(ParticleName, ActiveTime, E_normal){
	TTriangleMesh mesh;
	mesh.ReadFile(sourcefile.c_str(),0);
	mesh.Init();
	sourcearea = 0; // add triangles, whose vertices are all in the source volume, to sourcetris list
	for (CIterator i = geometry.mesh.triangles.begin(); i != geometry.mesh.triangles.end(); i++){
		if (mesh.InSolid(i->tri[0]) && mesh.InSolid(i->tri[1]) && mesh.InSolid(i->tri[2])){
			sourcetris.push_back(*i);
			sourcearea += i->area();
		}
	}
	printf("Source Area: %g m^2\n",sourcearea);
}


TSource::TSource(TConfig &geometryconf, TGeometry &geom, TFieldManager &field): source(NULL){
	sourcemode = geometryconf["SOURCE"].begin()->first; // only first source in geometry.in is read in
	istringstream sourceconf(geometryconf["SOURCE"].begin()->second);
	string ParticleName;
	sourceconf >> ParticleName;

	double ActiveTime;
	bool PhaseSpaceWeighting;
	if (sourcemode == "boxvolume"){
		double x_min, x_max, y_min, y_max, z_min, z_max;
		sourceconf >> x_min >> x_max >> y_min >> y_max >> z_min >> z_max >> ActiveTime >> PhaseSpaceWeighting;
		if (sourceconf)
			source = new TCuboidVolumeSource(ParticleName, ActiveTime, PhaseSpaceWeighting, x_min, x_max, y_min, y_max, z_min, z_max);
	}
	else if (sourcemode == "cylvolume"){
		double r_min, r_max, phi_min, phi_max, z_min, z_max;
		sourceconf >> r_min >> r_max >> phi_min >> phi_max >> z_min >> z_max >> ActiveTime >> PhaseSpaceWeighting;
		if (sourceconf)
			source = new TCylindricalVolumeSource(ParticleName, ActiveTime, PhaseSpaceWeighting, r_min, r_max, phi_min*conv, phi_max*conv, z_min, z_max);
	}
	else if (sourcemode == "STLvolume"){
		string sourcefile;
		sourceconf >> sourcefile >> ActiveTime >> PhaseSpaceWeighting;
		if (sourceconf)
			source = new TSTLVolumeSource(ParticleName, ActiveTime, PhaseSpaceWeighting, sourcefile);
	}
	else if (sourcemode == "cylsurface"){
		double r_min, r_max, phi_min, phi_max, z_min, z_max, E_normal;
		sourceconf >> r_min >> r_max >> phi_min >> phi_max >> z_min >> z_max >> ActiveTime >> E_normal;
		if (sourceconf)
			source = new TCylindricalSurfaceSource(ParticleName, ActiveTime, geom, E_normal, r_min, r_max, phi_min*conv, phi_max*conv, z_min, z_max);
	}
	else if (sourcemode == "STLsurface"){
		string sourcefile;
		double E_normal;
		sourceconf >> sourcefile >> ActiveTime >> E_normal;
		if (sourceconf)
			source = new TSTLSurfaceSource(ParticleName, ActiveTime, geom, sourcefile, E_normal);
	}

	if (!source){
		cout << "\nCould not load source """ << sourcemode << """! Did you enter invalid parameters?\n";
		exit(-1);
	}
	cout << '\n';
}


TSource::~TSource(){
	if (source)
		delete source;
}


TParticle* TSource::CreateParticle(TMCGenerator &mc, TGeometry &geometry, TFieldManager *field){
	return source->CreateParticle(mc, geometry, field);
}

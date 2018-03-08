

#include "Lubrication.hpp"

YADE_PLUGIN((Ip2_ElastMat_ElastMat_LubricationPhys)(LubricationPhys)(Law2_ScGeom_LubricationPhys)(Law2_ScGeom_ImplicitLubricationPhys))


LubricationPhys::LubricationPhys(const ViscElPhys &obj) :
    ViscElPhys(obj),
    eta(1.),
    eps(0.001),
    kno(0.),
    kso(0.),
    nun(0.),
    mum(0.3),
    ue(0.),
    contact(false),
    slip(false)
{
}

LubricationPhys::~LubricationPhys()
{

}

CREATE_LOGGER(LubricationPhys);

void Ip2_ElastMat_ElastMat_LubricationPhys::go(const shared_ptr<Material> &material1, const shared_ptr<Material> &material2, const shared_ptr<Interaction> &interaction)
{
    if (interaction->phys)
        return;

    // Cast to Lubrication
    shared_ptr<LubricationPhys> phys(new LubricationPhys());
    FrictMat* mat1 = YADE_CAST<FrictMat*>(material1.get());
    FrictMat* mat2 = YADE_CAST<FrictMat*>(material2.get());
    if(otherPhysFunctor)
    {
        otherPhysFunctor->go(material1,material2,interaction);
        phys->otherPhys = interaction->phys;
    }

    // Copy from HertzMindlin
    /* from interaction physics */
    Real Ea = mat1->young;
    Real Eb = mat2->young;
    Real Va = mat1->poisson;
    Real Vb = mat2->poisson;
    Real fa = mat1->frictionAngle;
    Real fb = mat2->frictionAngle;


    /* from interaction geometry */
    GenericSpheresContact* scg = YADE_CAST<GenericSpheresContact*>(interaction->geom.get());
    Real Da = scg->refR1>0 ? scg->refR1 : scg->refR2;
    Real Db = scg->refR2;
    //Vector3r normal=scg->normal;        //The variable set but not used


    /* calculate stiffness coefficients */
    Real Ga = Ea/(2*(1+Va));
    Real Gb = Eb/(2*(1+Vb));
    Real G = (Ga+Gb)/2; // average of shear modulus
    Real V = (Va+Vb)/2; // average of poisson's ratio
    Real E = Ea*Eb/((1.-std::pow(Va,2))*Eb+(1.-std::pow(Vb,2))*Ea); // Young modulus
    Real R = Da*Db/(Da+Db); // equivalent radius
    Real a = (Da+Db)/2.;
    Real Kno = 4./3.*E*sqrt(R); // coefficient for normal stiffness
    Real Kso = 2*sqrt(4*R)*G/(2-V); // coefficient for shear stiffness
    Real frictionAngle = std::min(fa,fb);
    const Real pi(3.141596);

    phys->kno = Kno;
    phys->kso = Kso;
    phys->mum = std::tan(frictionAngle);
    phys->nun = pi*eta*3./2.*a*a;
    //phys->delta = -1.;

    phys->eta = eta;
    phys->eps = eps;
    interaction->phys = phys;
}
CREATE_LOGGER(Ip2_ElastMat_ElastMat_LubricationPhys);

bool Law2_ScGeom_LubricationPhys::go(shared_ptr<IGeom> &iGeom, shared_ptr<IPhys> &iPhys, Interaction *interaction)
{
    // Physic
    LubricationPhys* phys=static_cast<LubricationPhys*>(iPhys.get());

    otherLawFunctor->scene = scene;

    // If not activated, only compute other law
    /*if(!activateLubrication)
            return (otherLawFunctor) ? otherLawFunctor->go(iGeom,phys->otherPhys,interaction) : false;//*/

    // Geometry
    ScGeom* geom=static_cast<ScGeom*>(iGeom.get());
    Vector3r norm = geom->normal/geom->normal.norm();

    // Get bodies properties
    Body::id_t id1 = interaction->getId1();
    Body::id_t id2 = interaction->getId2();
    const shared_ptr<Body> b1 = Body::byId(id1,scene);
    const shared_ptr<Body> b2 = Body::byId(id2,scene);
    State* s1 = b1->state.get();
    State* s2 = b2->state.get();

    // geometric parameters
    Real a((geom->radius1+geom->radius2)/2.);
    Real h(geom->penetrationDepth);
    const Real pi(3.141596);

    // Speeds
    Vector3r normVel((s2->vel-s1->vel).dot(norm)*norm);
    Vector3r normRot((s2->vel-s1->vel).cross(norm)/(2.*a+h));
    Vector3r shearVel((geom->radius1*(s1->angVel-normRot)+geom->radius2*(s2->angVel-normRot)).cross(norm));


    // Forces and torques
    Vector3r Fln = Vector3r::Zero();
    if(activateNormalLubrication) Fln = pi*phys->eta*3./2.*a*a/h*normVel;
    Vector3r FLs = Vector3r::Zero();
    if(activateTangencialLubrication) FLs = pi*phys->eta/2.*(-2.*a+(2.*a+h)*std::log((2.*a+h)/h))*shearVel;
    Vector3r Cr  = Vector3r::Zero();
    if(activateRollLubrication) Cr = pi*phys->eta*a*a*a*(3./2.*std::log(a/h)+63./500.*h/a*std::log(a/h))*((s1->angVel-s2->angVel)-((s1->angVel-s2->angVel).dot(norm))*norm);
    Vector3r Ct  = Vector3r::Zero();
    if(activateTwistLubrication) Ct = pi*phys->eta*a*h*std::log(a/h)*(s1->angVel-s2->angVel).dot(norm)*norm;

    // total torque
    Vector3r C1 = (geom->radius1+h/2.)*FLs.cross(norm)+Cr+Ct;
    Vector3r C2 = (geom->radius2+h/2.)*FLs.cross(norm)-Cr-Ct;

    //cout << "FL (" << Fln << ") FLs (" << FLs << ") Cr ("<< Cr << ") Ct (" << Ct << ")\n";

    // Apply!
    scene->forces.addForce(id1,Fln+FLs);
    scene->forces.addTorque(id1,C1);

    scene->forces.addForce(id2,-Fln-FLs);
    scene->forces.addTorque(id2,C2);

    // Compute other law

    if(otherLawFunctor)
        otherLawFunctor->go(iGeom, phys->otherPhys,interaction);

    NormShearPhys* ophys=YADE_CAST<NormShearPhys*>(phys->otherPhys.get());

    if(ophys)
    {
        phys->kn = ophys->kn;
        phys->ks = ophys->ks;
    }

    ViscElPhys* ophysel=YADE_CAST<ViscElPhys*>(phys->otherPhys.get());

    if(ophysel)
    {
        phys->cn += ophysel->cn;
        phys->cs += ophysel->cs;
    }

    return true;
}
CREATE_LOGGER(Law2_ScGeom_LubricationPhys);

bool Law2_ScGeom_ImplicitLubricationPhys::go(shared_ptr<IGeom> &iGeom, shared_ptr<IPhys> &iPhys, Interaction *interaction)
{

    // Physic
    LubricationPhys* phys=static_cast<LubricationPhys*>(iPhys.get());

    // Geometry
    ScGeom* geom=static_cast<ScGeom*>(iGeom.get());
    Vector3r norm = geom->normal/geom->normal.norm();

    // Get bodies properties
    Body::id_t id1 = interaction->getId1();
    Body::id_t id2 = interaction->getId2();
    const shared_ptr<Body> b1 = Body::byId(id1,scene);
    const shared_ptr<Body> b2 = Body::byId(id2,scene);
    State* s1 = b1->state.get();
    State* s2 = b2->state.get();

    // geometric parameters
    Real a((geom->radius1+geom->radius2)/2.);
    Real un(-geom->penetrationDepth);
    const Real pi(3.141596);
    
    // Speeds
    Vector3r shiftVel=scene->isPeriodic ? Vector3r(scene->cell->velGrad*scene->cell->hSize*interaction->cellDist.cast<Real>()) : Vector3r::Zero();
    Vector3r shift2 = scene->isPeriodic ? Vector3r(scene->cell->hSize*interaction->cellDist.cast<Real>()): Vector3r::Zero();

    Vector3r relV = geom->getIncidentVel(s1, s2, scene->dt, shift2, shiftVel, false );
//    Vector3r relVN = relV.dot(norm)*norm; // Normal velocity
//    Vector3r relVT = relV - relVN; // Tangeancial velocity
    Real undot = relV.dot(norm); // Normal velocity norm
    
    if(un > a)
    {
        return undot < 0; // Only go to potential if distance is increasing
    }

    // Normal force
    Vector3r Fn(Vector3r::Zero());
    Real ue(0.);
    Real delt = max(std::abs(phys->ue),a/100.);
    Real EPS = phys->eps*a;
    
    Real g = 3./2.*phys->kno*std::pow(delt,0.5); // Stiffness for normal surface deflection
    bool contact = un < phys->eps*a;
    Real kn = (contact) ? g : 0.;

    if(debug && contact && !phys->contact) LOG_INFO("CONTACT");
    if(debug && !contact && phys->contact) LOG_INFO("END OF CONTACT");

    
    phys->normalContactForce = Vector3r::Zero();
    phys->normalLubricationForce = Vector3r::Zero();
    
    if(activateNormalLubrication)
    {
        Real A = -g;
        Real B = (-g*un - kn*un + EPS*kn - phys->nun/scene->dt);
        Real C = -kn*un*(un-EPS) - phys->nun*undot + phys->nun*phys->ue/scene->dt;
        
        Real rho = B*B-4.*A*C;
        
        if(rho >= 0)
        {
            Real ue1 = (-B+std::sqrt(rho))/(2*A);
            Real ue2 = (-B-std::sqrt(rho))/(2*A);
            
            ue = (std::abs(ue1 - phys->ue) < std::abs(ue2-phys->ue)) ? ue1 : ue2;
        }
        else
        {
            LOG_WARN("rho < 0");
            ue = phys->ue;
        }
        
        Fn = -ue*g*norm;
        // Calculate separate forces
        phys->normalContactForce = (un - EPS)*kn*norm;
        
        if(phys->nun > 0.)
            phys->normalLubricationForce = phys->nun*(undot + (ue - phys->ue)/scene->dt)/(un + ue)*norm;
    }
    
    
    
    // Tangencial force
    Real u = ue + un;
    Vector3r Ft(Vector3r::Zero());
    Vector3r Ft_ = geom->rotate(phys->shearForce);
    const Vector3r& dus = geom->shearIncrement();
    Real kt = phys->kso*std::pow(delt,0.5);
    Real nut = (phys->eta > 0.) ? pi*phys->eta/2.*(-2.*a+(2.*a+u)*std::log((2.*a+u)/u)) : 0.;
   
    if(debug) LOG_DEBUG("#" << scene->iter << " nut " << nut << " a " << a << " u " << u);
    
    if(activateTangencialLubrication)
    {
        phys->slip = false;
        
        if(phys->eta > 0.)
        {
            if(contact)
            {
                Ft = Ft_ + kt*dus; // Trial force
                
                phys->shearContactForce = Ft;
                phys->shearLubricationForce = Vector3r::Zero();
#if 1
                if(Ft.norm() > phys->normalContactForce.norm()*std::max(0.,phys->mum)) // If slip
                {
                    //LOG_INFO("SLIP");
                    Ft *= phys->normalContactForce.norm()*std::max(0.,phys->mum)/Ft.norm();
                    phys->shearContactForce = Ft;
                    Ft = (Ft*kt*scene->dt + Ft_*nut + dus*kt*nut)/(kt*scene->dt+nut);
                    phys->slip = true;
                    phys->shearLubricationForce = nut*dus/scene->dt;
                }
#endif
            }
            else
            {
                Ft = (Ft_ + dus*kt)*nut/(nut+kt*scene->dt);
                
                phys->shearContactForce = Vector3r::Zero();
                phys->shearLubricationForce = Ft;
            }
        }
        else
        {
            //* Only contact
            Ft_ += kt*dus; // Trial force with shear increment
            
            if(Ft_.norm() > phys->normalContactForce.norm()*std::max(0.,phys->mum))
            {
                Ft_ *= phys->normalContactForce.norm()*std::max(0.,phys->mum)/Ft_.norm();
                phys->slip = true;
            }
            
            Ft = phys->shearContactForce = (contact) ? Ft_ : Vector3r::Zero();
            phys->shearLubricationForce = Vector3r::Zero();//*/
        }
    }
    else
    {
        phys->shearLubricationForce = Vector3r::Zero();
        phys->shearContactForce = Vector3r::Zero();
        Ft = Vector3r::Zero();
    }
    

    // Update Physics memory
    phys->normalForce = Fn;
    phys->shearForce = Ft;
    
    phys->ue = ue;
    phys->un = un;
    phys->contact = contact;

#if 1
    if(!std::isfinite(phys->normalForce.norm()) || !std::isfinite(phys->shearForce.norm()))
    {
        LOG_INFO("normalForce" << phys->normalForce);
        LOG_INFO("normalContactForce "<< phys->normalContactForce);
        LOG_INFO("normalLubricationForce "<< phys->normalLubricationForce);
        LOG_INFO("shearForce" << phys->shearForce);
        LOG_INFO("shearContactForce "<< phys->shearContactForce);
        LOG_INFO("shearLubricationForce "<< phys->shearLubricationForce);
    }  
#endif

    // Rolling and twist torques
    Vector3r relAngularVelocity = geom->getRelAngVel(s1,s2,scene->dt);
    Vector3r relTwistVelocity = relAngularVelocity.dot(norm)*norm;
    Vector3r relRollVelocity = relAngularVelocity - relTwistVelocity;

    Vector3r Cr = Vector3r::Zero();
    if(activateRollLubrication && phys->eta > 0.) Cr = pi*phys->eta*a*a*a*(3./2.*std::log(a/un)+63./500.*un/a*std::log(a/un))*relRollVelocity;
    Vector3r Ct = Vector3r::Zero();
    if (activateTwistLubrication && phys->eta > 0.) Ct = pi*phys->eta*a*un*std::log(a/un)*relTwistVelocity;

    // total torque
    Vector3r C1 = -(geom->radius1+un/2.)*Ft.cross(norm)+Cr+Ct;
    Vector3r C2 = -(geom->radius2+un/2.)*Ft.cross(norm)-Cr-Ct;

    // Apply!
    scene->forces.addForce(id1,Fn+Ft);
    scene->forces.addTorque(id1,C1);

    scene->forces.addForce(id2,-Fn-Ft);
    scene->forces.addTorque(id2,C2);

    return true;
}

CREATE_LOGGER(Law2_ScGeom_ImplicitLubricationPhys);

void Law2_ScGeom_ImplicitLubricationPhys::getStressForEachBody(vector<Matrix3r>& NCStresses, vector<Matrix3r>& SCStresses, vector<Matrix3r>& NLStresses, vector<Matrix3r>& SLStresses )
{
  	const shared_ptr<Scene>& scene=Omega::instance().getScene();
	NCStresses.resize(scene->bodies->size());
	SCStresses.resize(scene->bodies->size());
	NLStresses.resize(scene->bodies->size());
	SLStresses.resize(scene->bodies->size());
	for (size_t k=0;k<scene->bodies->size();k++)
	{
		NCStresses[k]=Matrix3r::Zero();
		SCStresses[k]=Matrix3r::Zero();
		NLStresses[k]=Matrix3r::Zero();
		SLStresses[k]=Matrix3r::Zero();
	}
	FOREACH(const shared_ptr<Interaction>& I, *scene->interactions){
		if(!I->isReal()) continue;
		GenericSpheresContact* geom=YADE_CAST<GenericSpheresContact*>(I->geom.get());
		LubricationPhys* phys=YADE_CAST<LubricationPhys*>(I->phys.get());
		
		if(phys)
		{			
			Vector3r lV1 = (3.0/(4.0*Mathr::PI*pow(geom->refR1,3)))*((geom->contactPoint-Body::byId(I->getId1(),scene)->state->pos));
			Vector3r lV2 = Vector3r::Zero();
			if (!scene->isPeriodic)
				lV2 = (3.0/(4.0*Mathr::PI*pow(geom->refR2,3)))*((geom->contactPoint- (Body::byId(I->getId2(),scene)->state->pos)));
			else
				lV2 = (3.0/(4.0*Mathr::PI*pow(geom->refR2,3)))*((geom->contactPoint- (Body::byId(I->getId2(),scene)->state->pos + (scene->cell->hSize*I->cellDist.cast<Real>()))));

			NCStresses[I->getId1()] -= phys->normalContactForce*lV1.transpose();
			NCStresses[I->getId2()] += phys->normalContactForce*lV2.transpose();
			SCStresses[I->getId1()] -= phys->shearContactForce*lV1.transpose();
			SCStresses[I->getId2()] += phys->shearContactForce*lV2.transpose();
			NLStresses[I->getId1()] -= phys->normalLubricationForce*lV1.transpose();
			NLStresses[I->getId2()] += phys->normalLubricationForce*lV2.transpose();
			SLStresses[I->getId1()] -= phys->shearLubricationForce*lV1.transpose();
			SLStresses[I->getId2()] += phys->shearLubricationForce*lV2.transpose();
		}
	}
}

py::tuple Law2_ScGeom_ImplicitLubricationPhys::PyGetStressForEachBody()
{
	py::list nc, sc, nl, sl;
	vector<Matrix3r> NCs, SCs, NLs, SLs;
	getStressForEachBody(NCs, SCs, NLs, SLs);
	FOREACH(const Matrix3r& m, NCs) nc.append(m);
	FOREACH(const Matrix3r& m, SCs) sc.append(m);
	FOREACH(const Matrix3r& m, NLs) nl.append(m);
	FOREACH(const Matrix3r& m, SLs) sl.append(m);
	return py::make_tuple(nc, sc, nl, sl);
}



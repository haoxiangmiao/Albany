//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "ATO_Optimizer.hpp"
#include "ATO_Pareto_Optimizer.hpp"
#include "Teuchos_TestForException.hpp"
#include "ATO_Solver.hpp"

#ifdef ATO_USES_NLOPT
#include <nlopt.h>
#endif //ATO_USES_NLOPT

#ifdef ATO_USES_DOTK
#include "ATO_DOTk_Optimizer.hpp"
#endif //ATO_USES_DOTK

#include <algorithm>

namespace ATO {

/**********************************************************************/
Teuchos::RCP<Optimizer> 
OptimizerFactory::create(const Teuchos::ParameterList& optimizerParams)
/**********************************************************************/
{
  std::string optPackage = optimizerParams.get<std::string>("Package");

  if( optPackage == "OC"  )  return Teuchos::rcp(new Optimizer_OC(optimizerParams));

  else
  if( optPackage == "Pareto"  )  return Teuchos::rcp(new Optimizer_Pareto(optimizerParams));

#ifdef ATO_USES_NLOPT
  else
  if( optPackage == "NLopt"  )  return Teuchos::rcp(new Optimizer_NLopt(optimizerParams));
#endif //ATO_USES_NLOPT

#ifdef ATO_USES_DOTK
  else
  if( optPackage == "DOTk"  )  return Teuchos::rcp(new Optimizer_DOTk(optimizerParams));
#endif //ATO_USES_DOTK
  else
    TEUCHOS_TEST_FOR_EXCEPTION(
      true, Teuchos::Exceptions::InvalidParameter, std::endl 
      << "Error!  Optimization package: " << optPackage << " Unknown!" << std::endl 
      << "Valid options are\n"
      << "/t OC ... optimality criterion\n"
      << "/t Pareto ... pareto optimization\n"

#ifdef ATO_USES_NLOPT
      << "/t NLopt ... NLOPT library\n" 
#endif //ATO_USES_NLOPT

#ifdef ATO_USES_DOTK
      << "/t DOTk ... Design Optimization Toolkit library\n" 
#endif //ATO_USES_DOTK
      << std::endl);

}

/**********************************************************************/
Optimizer::Optimizer(const Teuchos::ParameterList& optimizerParams)
/**********************************************************************/
{ 

  solverInterface = NULL;
  comm = Teuchos::null;

  topology = optimizerParams.get<Teuchos::RCP<Topology> >("Topology");

  if( optimizerParams.isType<Teuchos::ParameterList>("Convergence Tests") ){
    const Teuchos::ParameterList& 
      convParams = optimizerParams.get<Teuchos::ParameterList>("Convergence Tests");
    convergenceChecker = Teuchos::rcp(new ConvergenceTest(convParams));
  } else {
    TEUCHOS_TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter, 
      std::endl << "Optimization convergence:  'Convergence Tests' ParameterList is required" << std::endl);
  }
}

/**********************************************************************/
Optimizer_OC::Optimizer_OC(const Teuchos::ParameterList& optimizerParams) :
Optimizer(optimizerParams)
/**********************************************************************/
{ 
  p = NULL;
  p_last = NULL;
  f = 0.0;
  f_last = 0.0;
  g = 0.0;
  g_last = 0.0;
  dfdp = NULL;
  dgdp = NULL;
  dvdp = NULL;

  _moveLimit     = optimizerParams.get<double>("Move Limiter");
  _stabExponent  = optimizerParams.get<double>("Stabilization Parameter");

  if( optimizerParams.isType<Teuchos::ParameterList>("Volume Enforcement") ){
    const Teuchos::ParameterList& 
      volParams = optimizerParams.get<Teuchos::ParameterList>("Volume Enforcement");

    _volConvTol    = volParams.get<double>("Convergence Tolerance");
    _volConstraint = volParams.get<double>("Target Volume Fraction");
    _volMaxIter    = volParams.get<int>("Maximum Iterations");
  
    if(volParams.isType<double>("Minimum Volume Fraction"))
      _minVolume    = volParams.get<double>("Minimum Volume Fraction");
    else _minVolume = 0.1;
    if(volParams.isType<double>("Maximum Volume Fraction"))
      _maxVolume    = volParams.get<double>("Maximum Volume Fraction");
    else _maxVolume = 1.0;
    if( volParams.isType<double>("Acceptable Tolerance") )
      _volAccpTol    = volParams.get<double>("Acceptable Tolerance");
    else _volAccpTol = _volConvTol;
    if( volParams.isType<bool>("Use Newton Search") )
      _useNewtonSearch = volParams.get<bool>("Use Newton Search");
    else _useNewtonSearch = true;

  } else
  TEUCHOS_TEST_FOR_EXCEPTION(
    true, Teuchos::Exceptions::InvalidParameter, std::endl 
    << "Error! Missing 'Volume Enforcement' ParameterList." << std::endl);
  

  if( optimizerParams.isType<Teuchos::ParameterList>("Constraint Enforcement") ){
    const Teuchos::ParameterList& 
      conParams = optimizerParams.get<Teuchos::ParameterList>("Constraint Enforcement");
    constraintGradient = conParams.get<std::string>("Constraint Gradient");
  } else
    constraintGradient = "None";

}

#ifdef ATO_USES_NLOPT
/**********************************************************************/
Optimizer_NLopt::Optimizer_NLopt(const Teuchos::ParameterList& optimizerParams) :
Optimizer(optimizerParams)
/**********************************************************************/
{ 
  p = NULL;
  p_last = NULL;
  f = 0.0;
  f_last = 0.0;
  opt = NULL;

  _volConstraint = optimizerParams.get<double>("Volume Fraction Constraint");
  _optMethod     = optimizerParams.get<std::string>("Method");
  _nIterations = 0;
}
#endif //ATO_USES_NLOPT

/**********************************************************************/
void
Optimizer::SetInterface(Solver* mySolverInterface)
/**********************************************************************/
{
  solverInterface = dynamic_cast<OptInterface*>(mySolverInterface);
  TEUCHOS_TEST_FOR_EXCEPTION(
    solverInterface == NULL, Teuchos::Exceptions::InvalidParameter, std::endl 
    << "Error! Dynamic cast of Solver* to OptInterface* failed." << std::endl);
}

/******************************************************************************/
Optimizer_OC::~Optimizer_OC()
/******************************************************************************/
{
  if( p      ) delete [] p;
  if( p_last ) delete [] p_last;
  if( dfdp   ) delete [] dfdp;
  if( dgdp   ) delete [] dgdp;
  if( dvdp   ) delete [] dvdp;
}

#ifdef ATO_USES_NLOPT
/******************************************************************************/
Optimizer_NLopt::~Optimizer_NLopt()
/******************************************************************************/
{
  if( p      ) delete [] p;
  if( p_last ) delete [] p_last;
}
#endif //ATO_USES_NLOPT

/******************************************************************************/
double
Optimizer::computeNorm(const double* p, int n)
/******************************************************************************/
{
  double norm = 0.0;
  for(int i=0; i<n; i++){
    norm += p[i]*p[i];
  }
  double gnorm = 0.0;
  comm->SumAll(&norm, &gnorm, 1);
  gnorm = (gnorm > 0.0) ? sqrt(gnorm) : 0.0;
  return gnorm;
}


/******************************************************************************/
double
Optimizer::computeDiffNorm(const double* p, const double* p_last, int n, bool printResult)
/******************************************************************************/
{
  double norm = 0.0;
  for(int i=0; i<n; i++){
    norm += pow(p[i]-p_last[i],2);
  }
  double gnorm = 0.0;
  comm->SumAll(&norm, &gnorm, 1);
  gnorm = (gnorm > 0.0) ? sqrt(gnorm) : 0.0;
  if(printResult && comm->MyPID()==0){
    std::cout << "************************************************************************" << std::endl;
    std::cout << "  Optimizer:  computed diffnorm is: " << gnorm << std::endl;
    std::cout << "************************************************************************" << std::endl;
  }
  return gnorm;
}

/******************************************************************************/
void
Optimizer_OC::Initialize()
/******************************************************************************/
{
  TEUCHOS_TEST_FOR_EXCEPTION (
    solverInterface == NULL, Teuchos::Exceptions::InvalidParameter, 
    std::endl << "Error! Optimizer requires valid Solver Interface" << std::endl);

  numOptDofs = solverInterface->GetNumOptDofs();

  p      = new double[numOptDofs];
  p_last = new double[numOptDofs];
  dfdp   = new double[numOptDofs];
  dvdp   = new double[numOptDofs];

  std::fill_n(p,      numOptDofs, topology->getInitialValue());
  std::fill_n(p_last, numOptDofs, 0.0);
  std::fill_n(dfdp,   numOptDofs, 0.0);
  std::fill_n(dvdp,   numOptDofs, 0.0);

  if( constraintGradient != "None" ){
    dgdp = new double[numOptDofs];
    std::fill_n(dgdp, numOptDofs, 0.0);
  }

  solverInterface->ComputeVolume(_optVolume);
  solverInterface->InitializeTopology(p);
}

/******************************************************************************/
void
Optimizer_OC::Optimize()
/******************************************************************************/
{

  double vol=0.0;

  if( constraintGradient == "Adjoint" )
    solverInterface->Compute(p, f, dfdp, g, dgdp);
  else 
    solverInterface->Compute(p, f, dfdp, g);

  for(int i=0; i<numOptDofs; i++) p_last[i] = p[i];
  solverInterface->ComputeVolume(p, vol, dvdp);

  computeUpdatedTopology();

  double global_f=0.0, pnorm = computeNorm(p, numOptDofs);
  comm->SumAll(&f, &global_f, 1);
  convergenceChecker->initNorm(global_f, pnorm);

  int iter=0;
  double volConstraint_last = _volConstraint;
  std::list<double> dgdv_vals;
  bool optimization_converged = false;
  while(!optimization_converged) {

    f_last = f; g_last = g;
    if( constraintGradient == "Adjoint" )
      solverInterface->Compute(p, f, dfdp, g, dgdp);
    else
      solverInterface->Compute(p, f, dfdp, g);

    solverInterface->ComputeVolume(p, vol, dvdp);

    for(int i=0; i<numOptDofs; i++) p_last[i] = p[i];

  
    if( g != 0.0 ){
      // if the constraint condition isn't satisfied, modify the volume budget.

      double deltav = 0.0;
      if( constraintGradient == "Adjoint" ){
        double dv = 0.001;
        _volConstraint += dv;
        computeUpdatedTopology();
        _volConstraint -= dv;
  
        double dg = 0.0;
        for(int i=0; i<numOptDofs; i++){
          dg += dgdp[i]*(p[i]-p_last[i]);
        }
        double global_dg = 0.0;
        comm->SumAll(&dg, &global_dg, 1);
        double dgdv = global_dg/dv;
        deltav = -g / dgdv;

      } else {
        if( _volConstraint != volConstraint_last ){
          dgdv_vals.push_back( (g - g_last)/(_volConstraint - volConstraint_last) );
          if( dgdv_vals.size() > 10 ) dgdv_vals.pop_front();
          std::list<double>::iterator it;
          double dgdv = 0.0;
          for(it=dgdv_vals.begin(); it!=dgdv_vals.end(); ++it) dgdv += *it;
          dgdv /= dgdv_vals.size();
          deltav = -g / dgdv;
        } else {
          deltav = 0.001;
        }
      }
    
      double _dvolLimit = 0.1*_volConstraint;
      if(fabs(deltav) > _dvolLimit) deltav = deltav/fabs(deltav) * _dvolLimit;

      volConstraint_last = _volConstraint;
      _volConstraint += deltav;
  
      if(_volConstraint < _minVolume) _volConstraint = _minVolume;
      if(_volConstraint > _maxVolume) _volConstraint = _maxVolume;

 
    }

    computeUpdatedTopology();


    if(comm->MyPID()==0.0){
      std::cout << "************************************************************************" << std::endl;
      std::cout << "** Optimization Status Check *******************************************" << std::endl;
      std::cout << "Status: Objective = " << f << std::endl;
    }

    double delta_f, ldelta_f = f-f_last;
    comm->SumAll(&ldelta_f, &delta_f, 1);
    double delta_p = computeDiffNorm(p, p_last, numOptDofs, /*result to cout*/ false);

    optimization_converged = convergenceChecker->isConverged(delta_f, delta_p, iter, comm->MyPID());

    iter++;
  }

  return;
}

/******************************************************************************/
void
ConvergenceTest::initNorm( double f, double pnorm )
/******************************************************************************/
{
    Teuchos::Array<Teuchos::RCP<ConTest> >::iterator it;
    for(it=conTests.begin(); it!=conTests.end(); it++)
      (*it)->initNorm(f, pnorm);
}

/******************************************************************************/
bool
ConvergenceTest::isConverged( double delta_f, double delta_p, int iter, int myPID )
/******************************************************************************/
{
    if(iter == 0) return false;

    bool writeToCout = false;
    if(myPID == 0) writeToCout = true;

    // check convergence based on user defined criteria
    if(writeToCout){
      std::cout << "************************************************************************" << std::endl;
      std::cout << "** Optimization Convergence Check **************************************" << std::endl;
    }
    std::vector<bool> results;
    Teuchos::Array<Teuchos::RCP<ConTest> >::iterator it;
    for(it=conTests.begin(); it!=conTests.end(); it++)
      results.push_back((*it)->passed(delta_f, delta_p, writeToCout));
    
    bool converged = false;
    if( comboType == AND ){
      converged = ( find(results.begin(),results.end(),false) == results.end() );
    } else 
    if( comboType == OR ){
      converged = ( find(results.begin(),results.end(),true) != results.end() );
    }
    if(writeToCout){
      if(converged){
        if( iter < minIterations )
          std::cout << "Converged, but continuing because min iterations not reached." << std::endl;
        else 
          std::cout << "Converged!" << std::endl;
      } else
        std::cout << "Not converged." << std::endl;
      std::cout << "************************************************************************" << std::endl;
    }

    if( iter < minIterations ) converged = false;

    // check iteration limit
    if( iter >= maxIterations && !converged ){
      converged = true;
      if(writeToCout){
        std::cout << "************************************************************************" << std::endl;
        std::cout << "************************************************************************" << std::endl;
        std::cout << "**********  Not converged.  Exiting due to iteration limit.  ***********" << std::endl;
        std::cout << "************************************************************************" << std::endl;
        std::cout << "************************************************************************" << std::endl;
      }
    }

    return converged;
}

/******************************************************************************/
ConvergenceTest::ConvergenceTest(const Teuchos::ParameterList& convParams)
/******************************************************************************/
{

  if( convParams.isType<int>("Minimum Iterations") )
    minIterations = convParams.get<int>("Minimum Iterations");
  else minIterations = 0;

  if( convParams.isType<int>("Maximum Iterations") ){
    maxIterations = convParams.get<int>("Maximum Iterations");
  } else
    TEUCHOS_TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter, 
      std::endl << "Optimization convergence:  'Maximum Iterations' parameter is required." << std::endl);

  if( convParams.isType<std::string>("Combo Type") ){
    std::string combo = convParams.get<std::string>("Combo Type");
    std::transform(combo.begin(), combo.end(), combo.begin(), ::tolower);
    if(combo == "or"){
      comboType = OR;
    }else
    if(combo == "and"){
      comboType = AND;
    }else {
      TEUCHOS_TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter, 
        std::endl << "Optimization convergence:  Unknown 'Combo Type'.  Options are ('AND', 'OR') " << std::endl);
    }
  } else comboType = OR;

  if( convParams.isType<double>("Relative Topology Change") ){
    double conValue = convParams.get<double>("Relative Topology Change");
    conTests.push_back( Teuchos::rcp(new RelDeltaP(conValue)) );
  }
  if( convParams.isType<double>("Absolute Topology Change") ){
    double conValue = convParams.get<double>("Absolute Topology Change");
    conTests.push_back( Teuchos::rcp(new AbsDeltaP(conValue)) );
  }
  if( convParams.isType<double>("Relative Objective Change") ){
    double conValue = convParams.get<double>("Relative Objective Change");
    conTests.push_back( Teuchos::rcp(new RelDeltaF(conValue)) );
  }
  if( convParams.isType<double>("Absolute Objective Change") ){
    double conValue = convParams.get<double>("Absolute Objective Change");
    conTests.push_back( Teuchos::rcp(new AbsDeltaF(conValue)) );
  }
  if( convParams.isType<double>("Relative Objective Running Average Change") ){
    double conValue = convParams.get<double>("Relative Objective Running Average Change");
    conTests.push_back( Teuchos::rcp(new RelRunningDF(conValue)) );
  }
  if( convParams.isType<double>("Absolute Objective Running Average Change") ){
    double conValue = convParams.get<double>("Absolute Objective Running Average Change");
    conTests.push_back( Teuchos::rcp(new AbsRunningDF(conValue)) );
  }
}

/******************************************************************************/
bool ConvergenceTest::AbsDeltaP::passed(double delta_f, double delta_p, bool write)
{ 
  bool status = ( fabs(delta_p) < conValue );
  if( write )
    std::cout << "Test: Topology Change (Absolute): " << std::endl 
    << "     abs(dp) = " << fabs(delta_p) << " < " << conValue << ": " 
    << (status ? "true" : "false") << std::endl;
  return status;
}
bool ConvergenceTest::AbsDeltaF::passed(double delta_f, double delta_p, bool write)
{
  bool status = ( fabs(delta_f) < conValue );
  if( write )
    std::cout << "Test: Objective Change (Absolute): " << std::endl 
    << "     abs(df) = " << fabs(delta_f) << " < " << conValue << ": " 
    << (status ? "true" : "false") << std::endl;
  return status;
}
bool ConvergenceTest::AbsRunningDF::passed(double delta_f, double delta_p, bool write)
{
  dF.push_back(delta_f);
  runningDF += delta_f;
  if(dF.size()>nave) runningDF -= *(dF.end()-nave);
  bool status = ( runningDF < conValue );
  if( write )
    std::cout << "Test: Objective Change Running Average (Absolute): " << std::endl 
    << "     abs(<df>) = " << runningDF << " < " << conValue << ": " 
    << (status ? "true" : "false") << std::endl;
  return status;
}
bool ConvergenceTest::RelDeltaP::passed(double delta_f, double delta_p, bool write){
  bool status = (p0 != 0.0) ? ( fabs(delta_p/p0) < conValue ) : false;
  if( write )
    std::cout << "Test: Topology Change (Relative): " << std::endl 
    << "     abs(dp) = " << fabs(delta_p) << ", fabs(dp/p0) = " << fabs(delta_p/p0) << " < " << conValue 
    << ": " << (status ? "true" : "false") << std::endl;
  return status;
}
bool ConvergenceTest::RelDeltaF::passed(double delta_f, double delta_p, bool write){
  bool status = (f0 != 0.0) ? ( fabs(delta_f/f0) < conValue ) : false;
  if( write )
    std::cout << "Test: Objective Change (Relative): " << std::endl 
    << "     abs(df) = " << fabs(delta_f) << ", fabs(df/f0) = " << fabs(delta_f/f0) << " < " << conValue 
    << ": " << (status ? "true" : "false") << std::endl;
  return status;
}
bool ConvergenceTest::RelRunningDF::passed(double delta_f, double delta_p, bool write){
  dF.push_back(delta_f);
  runningDF += delta_f;
  int nvals = dF.size();
  int lastVal = nvals-1;
  if(nvals>nave){
     runningDF -= dF[lastVal-nave];
     nvals = nave;
   }
  bool status = (f0 != 0.0) ? ( fabs(runningDF/f0)/nvals < conValue ) : false;
  if( write )
    std::cout << "Test: Objective Change Running Average (Relative): " << std::endl 
    << "     abs(<df>) = " << fabs(runningDF)/nvals << ", fabs(<df/f0>) = " << fabs(runningDF/f0)/nvals << " < " << conValue 
    << ": " << (status ? "true" : "false") << std::endl;
  return status;
}
/******************************************************************************/


/******************************************************************************/
void
Optimizer_OC::computeUpdatedTopology()
/******************************************************************************/
{

  // find multiplier that enforces volume constraint
  const Teuchos::Array<double>& bounds = topology->getBounds();
  const double minDensity = bounds[0];
  const double maxDensity = bounds[1];
  const double offset = minDensity - 0.01*(maxDensity-minDensity);
  double vmid, v1=0.0, v2=0.0;
  double residRatio = 0.0;
  int niters=0;

  double dfdp_tot = 0.0, dvdp_tot = 0.0;
  for(int i=0; i<numOptDofs; i++) {
    dfdp_tot += dfdp[i];
    dvdp_tot += dvdp[i];
  }
  double g_dfdp_tot = 0.0, g_dvdp_tot = 0.0;
  comm->SumAll(&dfdp_tot, &g_dfdp_tot, 1);
  comm->SumAll(&dvdp_tot, &g_dvdp_tot, 1);

  v2 = -10.0* g_dfdp_tot / g_dvdp_tot;

  if(comm->MyPID()==0){
    std::cout << "Volume enforcement: Target = " << _volConstraint <<  std::endl;
    std::cout << "Volume enforcement: Beginning search with recursive bisection." <<  std::endl;
  }

  bool converged = false;
  double vol = 0.0;
  do {
    vol = 0.0;
    vmid = (v2+v1)/2.0;

    // update topology
    for(int i=0; i<numOptDofs; i++) {
      double be = -dfdp[i]/dvdp[i]/vmid;
      double p_old = p_last[i];
      double p_new = (p_old-offset)*pow(be,_stabExponent)+offset;
      // limit change
      double dval = p_new - p_old;
      if( fabs(dval) > _moveLimit) p_new = p_old+fabs(dval)/dval*_moveLimit;
      // enforce limits
      if( p_new < minDensity ) p_new = minDensity;
      if( p_new > maxDensity ) p_new = maxDensity;
      p[i] = p_new;
    }

    // compute new volume
    if( _useNewtonSearch ){
      double prevResidual = vol - _volConstraint*_optVolume;
      solverInterface->ComputeVolume(p, vol);
      double newResidual = vol - _volConstraint*_optVolume;
      if( newResidual > 0.0 ){
        residRatio = newResidual/prevResidual;
        v1 = vmid;
        niters++;
        break;
      } else v2 = vmid;
    } else {
      solverInterface->ComputeVolume(p, vol);
      double newResidual = vol - _volConstraint*_optVolume;
      if( newResidual > 0.0 ){
        v1 = vmid;
      } else v2 = vmid;
    }
    niters++;

    if(comm->MyPID()==0){
      double resid = (vol - _volConstraint*_optVolume)/_optVolume;
      std::cout << "Volume enforcement (iteration " << niters << "): Residual = " << resid << std::endl;
    }

  } while ( niters < _volMaxIter && fabs(vol - _volConstraint*_optVolume) > _volConvTol*_optVolume );


  if(_useNewtonSearch){

  if(comm->MyPID()==0){
    std::cout << "Volume enforcement: Bounds found.  Switching to Newton search." << std::endl;
  }

  int newtonMaxIters = niters + 10;
  double lambda = (residRatio*v2 - v1)/(residRatio-1.0);
  double epsilon = lambda*1e-5;
  if( lambda > 0.0 ) do {
    for(int i=0; i<numOptDofs; i++) {
      double be = -dfdp[i]/dvdp[i]/lambda;
      double p_old = p_last[i];
      double p_new = (p_old-offset)*pow(be,_stabExponent)+offset;
      // limit change
      double dval = p_new - p_old;
      if( fabs(dval) > _moveLimit) p_new = p_old+fabs(dval)/dval*_moveLimit;
      // enforce limits
      if( p_new < minDensity ) p_new = minDensity;
      if( p_new > maxDensity ) p_new = maxDensity;
      p[i] = p_new;
    }
    // compute new volume
    solverInterface->ComputeVolume(p, vol);
    double f0 =  (vol - _volConstraint*_optVolume);

    if(comm->MyPID()==0){
      std::cout << "Volume Enforcement (iteration " << niters << "): Residual = " << f0/_optVolume << std::endl;
    }

    if( fabs(f0) < _volConvTol*_optVolume ){
      converged = true;
      break;
    }

    double plambda = lambda+epsilon;
    for(int i=0; i<numOptDofs; i++) {
      double be = -dfdp[i]/dvdp[i]/plambda;
      double p_old = p_last[i];
      double p_new = (p_old-offset)*pow(be,_stabExponent)+offset;
      // limit change
      double dval = p_new - p_old;
      if( fabs(dval) > _moveLimit) p_new = p_old+fabs(dval)/dval*_moveLimit;
      // enforce limits
      if( p_new < minDensity ) p_new = minDensity;
      if( p_new > maxDensity ) p_new = maxDensity;
      p[i] = p_new;
    }
    // compute new volume
    solverInterface->ComputeVolume(p, vol);
    double f1 =  (vol - _volConstraint*_optVolume);

    if( f1-f0 == 0.0 ) break;
    lambda -= epsilon*f0/(f1-f0);

    niters++;
  } while ( niters < newtonMaxIters );

  if(!converged){
    if(comm->MyPID()==0){
      std::cout << "Volume enforcement: Newton search failed.  Switching back to recursive bisection." << std::endl;
    }
  
    niters = 0;
    do {
      vol = 0.0;
      vmid = (v2+v1)/2.0;
  
      // update topology
      for(int i=0; i<numOptDofs; i++) {
        double be = -dfdp[i]/dvdp[i]/vmid;
        double p_old = p_last[i];
        double p_new = (p_old-offset)*pow(be,_stabExponent)+offset;
        // limit change
        double dval = p_new - p_old;
        if( fabs(dval) > _moveLimit) p_new = p_old+fabs(dval)/dval*_moveLimit;
        // enforce limits
        if( p_new < minDensity ) p_new = minDensity;
        if( p_new > maxDensity ) p_new = maxDensity;
        p[i] = p_new;
      }
  
      // compute new volume
      solverInterface->ComputeVolume(p, vol);
      double newResidual = vol - _volConstraint*_optVolume;
      if( newResidual > 0.0 ){
        v1 = vmid;
      } else v2 = vmid;
      niters++;
  
      if(comm->MyPID()==0){
        double resid = (vol - _volConstraint*_optVolume)/_optVolume;
        std::cout << "Volume enforcement (iteration " << niters << "): Residual = " << resid << std::endl;
      }
    
    } while ( niters < _volMaxIter && fabs(vol - _volConstraint*_optVolume) > _volConvTol*_optVolume );
  }

  }

  TEUCHOS_TEST_FOR_EXCEPTION(
    ( fabs(vol - _volConstraint*_optVolume) > _volAccpTol*_optVolume ),
    Teuchos::Exceptions::InvalidParameter, 
    std::endl << "Enforcement of volume constraint failed:  Exceeded max iterations" 
    << std::endl);



}

#ifdef ATO_USES_NLOPT
/******************************************************************************/
void
Optimizer_NLopt::Initialize()
/******************************************************************************/
{
  TEUCHOS_TEST_FOR_EXCEPTION (
    solverInterface == NULL, Teuchos::Exceptions::InvalidParameter, std::endl
    << "Error! Optimizer requires valid Solver Interface" << std::endl);

  TEUCHOS_TEST_FOR_EXCEPTION (
    (comm->NumProc() != 1) && (comm->MyPID()==0), 
    Teuchos::Exceptions::InvalidParameter, std::endl
    << "Error! NLopt package doesn't work in parallel.  Use OC package." << std::endl);
  TEUCHOS_TEST_FOR_EXCEPT ( (comm->NumProc() != 1) );

  numOptDofs = solverInterface->GetNumOptDofs();
 
  const Teuchos::Array<double>& bounds = topology->getBounds();
  const double minDensity = bounds[0];
  const double maxDensity = bounds[1];
  
  if( _optMethod == "MMA" )
    opt = nlopt_create(NLOPT_LD_MMA, numOptDofs);
  else
  if( _optMethod == "CCSA" )
    opt = nlopt_create(NLOPT_LD_CCSAQ, numOptDofs);
  else
    TEUCHOS_TEST_FOR_EXCEPTION(
      true, Teuchos::Exceptions::InvalidParameter, std::endl 
      << "Error!  Optimization method: " << _optMethod << " Unknown!" << std::endl 
      << "Valid options are (MMA)" << std::endl);

  
  // set bounds
  nlopt_set_lower_bounds1(opt, minDensity);
  nlopt_set_upper_bounds1(opt, maxDensity);

  // set objective function
  nlopt_set_min_objective(opt, this->evaluate, this);
  
  // set stop criteria
  nlopt_set_xtol_rel(opt, 1e-9);  // don't converge based on this.  I.e., use convergenceChecker.
  nlopt_set_maxeval(opt, convergenceChecker->getMaxIterations());

  // set volume constraint
  nlopt_add_inequality_constraint(opt, this->constraint, this, _volConvTol*_optVolume);

  p      = new double[numOptDofs];
  p_last = new double[numOptDofs];

  std::fill_n(p,      numOptDofs, topology->getInitialValue());
  std::fill_n(p_last, numOptDofs, 0.0);

  solverInterface->ComputeVolume(_optVolume);
}

#define ATO_XTOL_REACHED 104

/******************************************************************************/
void
Optimizer_NLopt::Optimize()
/******************************************************************************/
{
  double* dfdp_init = new double[numOptDofs];
  solverInterface->ComputeObjective(p, f, dfdp_init);
  delete [] dfdp_init;
  double global_f=0.0, pnorm = computeNorm(p, numOptDofs);
  comm->SumAll(&f, &global_f, 1);
  convergenceChecker->initNorm(global_f, pnorm);

  double minf;
  int errorcode = nlopt_optimize(opt, p, &minf);

  if( errorcode == NLOPT_FORCED_STOP && comm->MyPID()==0 ){
    int forcestop_errorcode = nlopt_get_force_stop(opt);
    std::cout << "************************************************************************" << std::endl;
    std::cout << "  Optimizer converged.  Objective value = " << minf << std::endl;
    std::cout << "    Convergence code: " << forcestop_errorcode << std::endl;
    std::cout << "************************************************************************" << std::endl;
  } else
  if( errorcode < 0 ){
    TEUCHOS_TEST_FOR_EXCEPTION(
      true, Teuchos::Exceptions::InvalidParameter, std::endl 
      << "Error!  Optimization failed with errorcode " << errorcode << std::endl);
  }

  return;
}

/******************************************************************************/
double 
Optimizer_NLopt::evaluate_backend( unsigned int n, const double* x, double* grad )
/******************************************************************************/
{

  f_last = f;
  std::memcpy((void*)p_last, (void*)x, numOptDofs*sizeof(double));

  solverInterface->ComputeObjective(x, f, grad);


  if(comm->MyPID()==0){
    std::cout << "************************************************************************" << std::endl;
    std::cout << "  Optimizer:  objective value is: " << f << std::endl;
    std::cout << "************************************************************************" << std::endl;
  }

  double delta_f, ldelta_f = f-f_last;
  comm->SumAll(&ldelta_f, &delta_f, 1);
  double delta_p = computeDiffNorm(x, p_last, numOptDofs, /*result to cout*/ false);

  if( convergenceChecker->isConverged(delta_f, delta_p, _nIterations , comm->MyPID()) ){
    nlopt_set_force_stop(opt, ATO_XTOL_REACHED);
    nlopt_force_stop(opt);
  }
  _nIterations++;

  return f;
}

/******************************************************************************/
double 
Optimizer_NLopt::evaluate( unsigned int n, const double* x,
                           double* grad, void* data)
/******************************************************************************/
{
  Optimizer_NLopt* NLopt = reinterpret_cast<Optimizer_NLopt*>(data);
  return NLopt->evaluate_backend(n, x, grad);
}

/******************************************************************************/
double 
Optimizer_NLopt::constraint_backend( unsigned int n, const double* x, double* grad )
/******************************************************************************/
{
  double vol;
  solverInterface->ComputeVolume(x, vol, grad);

  if(comm->MyPID()==0){
    std::cout << "************************************************************************" << std::endl;
    std::cout << "  Optimizer:  computed volume is: " << vol << std::endl;
    std::cout << "************************************************************************" << std::endl;
  }
  return vol-_volConstraint*_optVolume;
}

/******************************************************************************/
double 
Optimizer_NLopt::constraint( unsigned int n, const double* x,
                             double* grad, void* data)
/******************************************************************************/
{
  Optimizer_NLopt* NLopt = reinterpret_cast<Optimizer_NLopt*>(data);
  return NLopt->constraint_backend(n, x, grad);
}
#endif //ATO_USES_NLOPT


}


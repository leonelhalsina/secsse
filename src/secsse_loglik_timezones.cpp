#include <vector>

#include <Rcpp.h>
using namespace Rcpp;

#include "odeint.h"
#include "util.h"

double calc_ll_timezone(const Rcpp::NumericVector& ll1,
                        const Rcpp::NumericVector& mm1,
                        const Rcpp::NumericMatrix& Q1,
                        const Rcpp::NumericVector& ll2,
                        const Rcpp::NumericVector& mm2,
                        const Rcpp::NumericMatrix& Q2,
                        const double crit_t,
                        const std::vector<int>& ances,
                        const std::vector< std::vector< double >>& for_time,
                        std::vector<std::vector<double>>& states,
                        Rcpp::NumericVector& merge_branch_out,
                        Rcpp::NumericVector& nodeM_out,
                        double absolute_tol,
                        double relative_tol,
                        std::string method) {
  
  ode_standard od1(ll1, mm1, Q1);
  ode_standard od2(ll2, mm2, Q2);
  
  ode_transition<ode_standard> master_od(od1, od2, crit_t);
  
  size_t d = ll1.size();
  
  long double loglik = 0.0;
  
  std::vector< double > mergeBranch(d);
  std::vector< double  > nodeN;
  std::vector< double  > nodeM;
  
  for (int a = 0; a < ances.size(); ++a) {
    int focal = ances[a];
    std::vector<int> desNodes;
    std::vector<double> timeInte;
    find_desNodes(for_time, focal, desNodes, timeInte);
    
    for (int i = 0; i < desNodes.size(); ++i) {
      int focal_node = desNodes[i];
      std::vector< double > y = states[focal_node - 1];
      
      std::unique_ptr<ode_transition<ode_standard>> od_ptr = std::make_unique<ode_transition<ode_standard>>(master_od);
      
      odeintcpp::integrate(method, 
                           std::move(od_ptr), // ode class object
                           y,// state vector
                           0.0,// t0
                           timeInte[i], //t1
                                   timeInte[i] * 0.01,
                                   absolute_tol,
                                   relative_tol); // t1
      
      if (i == 0) nodeN = y;
      if (i == 1) nodeM = y;
    }
    
    normalize_loglik_node(nodeM, loglik);
    normalize_loglik_node(nodeN, loglik);
    
    // code correct up till here.
    for (int i = 0; i < d; ++i) {
      mergeBranch[i] = nodeM[i + d] * nodeN[i + d] * ll1[i];
    }
    normalize_loglik(mergeBranch, loglik);
    
    std::vector< double > newstate(d);
    for (int i = 0; i < d; ++i) newstate[i] = nodeM[i];
    newstate.insert(newstate.end(), mergeBranch.begin(), mergeBranch.end());
    
    states[focal - 1] = newstate; // -1 because of R conversion to C++ indexing
  }
  
  merge_branch_out = NumericVector(mergeBranch.begin(), mergeBranch.end());
  nodeM_out = NumericVector(nodeM.begin(), nodeM.end());
  
  return loglik;
}

// [[Rcpp::export]]
Rcpp::List calThruNodes_timezones_cpp(const NumericVector& ances,
                                      const NumericMatrix& states_R,
                                      const NumericMatrix& forTime_R,
                                      const NumericVector& lambdas1,
                                      const NumericVector& mus1,
                                      const NumericMatrix& Q1,
                                      const NumericVector& lambdas2,
                                      const NumericVector& mus2,
                                      const NumericMatrix& Q2,
                                      const double crit_t,
                                      int num_threads,
                                      double abstol,
                                      double reltol,
                                      std::string method,
                                      bool is_complete_tree) {
  
  
  std::vector< std::vector< double >> states, forTime;
  
  numericmatrix_to_vector(states_R, states);
  numericmatrix_to_vector(forTime_R, forTime);
  
  NumericVector mergeBranch;
  NumericVector nodeM;
  
  double loglik;
  if (is_complete_tree) {
    Rcpp::stop("This is not implemented yet for multiple rates");
  } else {
    loglik = calc_ll_timezone(lambdas1,
                              mus1,
                              Q1,
                              lambdas2,
                              mus2,
                              Q2,
                              crit_t,
                              std::vector<int>(ances.begin(), ances.end()),
                              forTime,
                              states,
                              mergeBranch,
                              nodeM,
                              abstol,
                              reltol,
                              method);
  }
  
  NumericMatrix states_out;
  vector_to_numericmatrix(states, states_out);
  
  Rcpp::List output = Rcpp::List::create( Named("states") = states_out,
                                          Named("loglik") = loglik,
                                          Named("mergeBranch") = mergeBranch,
                                          Named("nodeM") = nodeM);
  return output;
}
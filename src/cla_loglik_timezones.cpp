#include <vector>
#include "odeint.h"
#include "util.h"

#include <Rcpp.h>
using namespace Rcpp;


std::vector< std::vector< std::vector< double > >> list_to_cpp(const Rcpp::List& ll) {
  std::vector< std::vector< std::vector< double > >> ll_cpp;
  for (size_t i = 0; i < ll.size(); ++i) {
    Rcpp::NumericMatrix temp = ll[i];
    std::vector< std::vector< double >> temp2;
    for (size_t j = 0; j < temp.nrow(); ++j) {
      std::vector<double> row;
      for (size_t k = 0; k < temp.ncol(); ++k) {
        row.push_back(temp(j, k));  
      }
      temp2.push_back(row);
    }
    ll_cpp.push_back(temp2);
  }
  return ll_cpp;
}


double calc_ll_cla_timezones(const Rcpp::List& ll1,
                   const Rcpp::NumericVector& mm1,
                   const Rcpp::NumericMatrix& Q1,
                   const Rcpp::List& ll2,
                   const Rcpp::NumericVector& mm2,
                   const Rcpp::NumericMatrix& Q2,
                   const double crit_t,
                   const std::vector<int>& ances,
                   const std::vector< std::vector< double >>& for_time,
                   std::vector<std::vector<double>>& states,
                   Rcpp::NumericVector& merge_branch_out,
                   Rcpp::NumericVector& nodeM_out,
                   const std::string& method,
                   double absolute_tol,
                   double relative_tol) {
  
  auto ll1_cpp = list_to_cpp(ll1);
  auto ll2_cpp = list_to_cpp(ll2);
  
  std::vector<double> mm1_cpp(mm1.begin(), mm1.end());
  std::vector<double> mm2_cpp(mm2.begin(), mm2.end());
  
  std::vector< std::vector<double >> Q1_cpp;
  numericmatrix_to_vector(Q1, Q1_cpp);
  
  std::vector< std::vector<double >> Q2_cpp;
  numericmatrix_to_vector(Q2, Q2_cpp);
  
  ode_cla od1(ll1_cpp, mm1_cpp, Q1_cpp);
  ode_cla od2(ll2_cpp, mm2_cpp, Q2_cpp);
  
  ode_transition<ode_cla> master_od(od1, od2, crit_t);
  
  size_t d = od1.get_d();
  
  std::vector<double> mergeBranch(d);
  std::vector<double> nodeN;
  std::vector<double> nodeM;
  
  int max_ances = *std::max_element(ances.begin(), ances.end());
  std::vector< double > add(states[0].size(), 0.0);
  while (max_ances > states.size()) {
    states.push_back(add);
  }
  states.push_back(add);
  
  std::vector< double > logliks(ances.size());
  std::vector<double> y;
  
  std::vector<int> desNodes;
  std::vector<double> timeInte;
  long double loglik = 0;
  
  for (int a = 0; a < ances.size(); ++a) {
    
    int focal = ances[a];
    find_desNodes(for_time, focal, desNodes, timeInte);
    
    int focal_node;
    //  Rcpp::Rcout << a << " ";
    for (int i = 0; i < desNodes.size(); ++i) {
      focal_node = desNodes[i];
      assert((focal_node) >= 0);
      assert((focal_node) < states.size());
      
      y = states[focal_node];
      
      
      std::unique_ptr<ode_transition<ode_cla>> od_ptr = 
        std::make_unique<ode_transition<ode_cla>>(master_od);
      odeintcpp::integrate(method,
                           std::move(od_ptr), // ode class object
                           y, // state vector
                           0.0, // t0
                           timeInte[i], //t1
                                   timeInte[i] * 0.1,
                                   absolute_tol,
                                   relative_tol); // t1
      
      if (i == 0) nodeN = y;
      if (i == 1) nodeM = y;
    }
    
    normalize_loglik_node(nodeM, loglik); //Rcout << "nodeM: " << loglik<< "\n";
    normalize_loglik_node(nodeN, loglik); //Rcout << "nodeN: " << loglik<< "\n";
    
    mergeBranch = std::vector<double>(d, 0.0);
    
    for (size_t i = 0; i < d; ++i) {
      for (size_t j = 0; j < d; ++j) {
        for (size_t k = 0; k < d; ++k) {
          
          if (ll1_cpp[i][j][k] != 0.0) {
            mergeBranch[i] += ll1_cpp[i][j][k] * (nodeN[j + d] * nodeM[k + d] +
              nodeM[j + d] * nodeN[k + d]);
          }
        }
      }
      mergeBranch[i] *= 0.5;
    }
    
    normalize_loglik(mergeBranch, loglik);
    
    std::vector<double> newstate(d);
    for (int i = 0; i < d; ++i) newstate[i] = nodeM[i];
    newstate.insert(newstate.end(), mergeBranch.begin(), mergeBranch.end());
    
    assert((focal) >= 0);
    assert((focal) < states.size());
    states[focal] = newstate;
  }
  
  for (int i = 0; i < mergeBranch.size(); ++i) {
    merge_branch_out.push_back(mergeBranch[i]);
  }
  for (int i = 0; i < nodeM.size(); ++i) {
    nodeM_out.push_back(nodeM[i]);
  }
  
  return loglik;
}


// [[Rcpp::export]]
Rcpp::List cla_calThruNodes_timezones_cpp(const Rcpp::NumericVector& ances,
                                const Rcpp::NumericMatrix& states_R,
                                const Rcpp::NumericMatrix& forTime_R,
                                const Rcpp::List& lambdas1,
                                const Rcpp::NumericVector& mus1,
                                const Rcpp::NumericMatrix& Q1,
                                const Rcpp::List& lambdas2,
                                const Rcpp::NumericVector& mus2,
                                const Rcpp::NumericMatrix& Q2,
                                double crit_t,
                                std::string method,
                                double atol,
                                double rtol,
                                bool is_complete_tree) {
  
  try {
    std::vector< std::vector< double >> states, forTime;
    numericmatrix_to_vector(states_R, states);
    numericmatrix_to_vector(forTime_R, forTime);
    
    NumericVector mergeBranch;
    NumericVector nodeM;
    
    // Rcout << "welcome into cla_calThruNodes_cpp\n"; force_output();
    
    double loglik = 0.0;
    if (is_complete_tree) {
      Rcpp::stop("complete tree conditioning is not available with timezones");
    } else {
      loglik = calc_ll_cla_timezones(lambdas1,
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
                                      method, atol, rtol);
    }
    
    NumericMatrix states_out;
    vector_to_numericmatrix(states, states_out);
    
    Rcpp::List output = Rcpp::List::create( Named("states") = states_out,
                                            Named("loglik") = loglik,
                                            Named("mergeBranch") = mergeBranch,
                                            Named("nodeM") = nodeM);
    return output;
  } catch(std::exception &ex) {
    forward_exception_to_r(ex);
  } catch(...) {
    ::Rf_error("c++ exception (unknown reason)");
  }
  return NA_REAL;
}
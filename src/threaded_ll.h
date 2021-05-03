#ifndef THREADED_LL_HPP
#define THREADED_LL_HPP

#include <Rcpp.h>
using namespace Rcpp;

#include <vector>
#include <tuple>

#include <thread>
#include <chrono>

#include "odeint.h"
#include "util.h"

#include <RcppParallel.h>


using state_vec = std::vector<double>; 
using state_node = tbb::flow::function_node< state_vec, state_vec>;
using merge_node = tbb::flow::function_node< std::tuple<state_vec, state_vec>, state_vec>;
using join_node  = tbb::flow::join_node< std::tuple<state_vec, state_vec>, tbb::flow::queueing >;

template<typename OD_OBJECT>
struct update_state {
  
  update_state(double dt, int id,
               const OD_OBJECT& od) : dt_(dt), id_(id), od_(od) {}
  
  
  state_vec operator()(const state_vec& input) {
    state_vec current_state = input;
    // extract log likelihood:
    double loglik = current_state.back(); current_state.pop_back();
    
    bno::integrate(od_, current_state, 0.0, dt_, 0.1 * dt_);
    normalize_loglik_node(current_state, loglik);
    current_state.push_back(loglik);
    
    return current_state;
  }
  
  double dt_;
  int id_;
  OD_OBJECT od_;
};


struct collect_ll {
  state_vec &my_ll;
public:
  collect_ll( state_vec &ll ) : my_ll(ll) {}
  state_vec operator()( const state_vec& v ) {
    // my_sum += get<0>(v) + get<1>(v);
    my_ll = v;
    return my_ll;
  }
};


template <typename OD_OBJECT, typename MERGE_STATE>
struct threaded_ll {

private:  
  std::vector< state_node* > state_nodes;
  std::vector< merge_node* > merge_nodes;
  std::vector< join_node*  > join_nodes;
  
  tbb::flow::graph g;
  const OD_OBJECT od;
  const std::vector<int> ances;
  const std::vector< std::vector< double >> for_time;
  const std::vector<std::vector<double>> states;
  const int num_threads;
  const int d;

public:
  
  threaded_ll(const OD_OBJECT& od_in,
              const std::vector<int>& ances_in,
              const std::vector< std::vector< double >>& for_time_in,
              const std::vector< std::vector< double >>& states_in,
              int n_threads) :
  od(od_in), ances(ances_in), for_time(for_time_in), states(states_in), num_threads(n_threads), d(od_in.get_d()) {
  }
  
  Rcpp::List calc_ll() {
    state_nodes.clear();
    merge_nodes.clear();
    join_nodes.clear();
    
    tbb::task_scheduler_init _tbb((num_threads > 0) ? num_threads : tbb::task_scheduler_init::automatic);
    
    int num_tips = ances.size() + 1;

    // connect flow graph
    tbb::flow::broadcast_node<double> start(g);
    
    for (size_t i = 0; i < states.size() + 1; ++i) {
      double dt = get_dt(for_time, i); 
      auto new_node = new state_node(g, tbb::flow::unlimited, update_state<OD_OBJECT>(dt, i, od));
      state_nodes.push_back(new_node);
    }
    
    std::vector<int> connections;
    for (size_t i = 0; i < ances.size(); ++i) {
      connections = find_connections(for_time, ances[i]);
      
      auto new_join = new join_node(g);
      join_nodes.push_back(new_join);
      tbb::flow::make_edge(*state_nodes[connections[0]], std::get<0>(join_nodes.back()->input_ports()));
      tbb::flow::make_edge(*state_nodes[connections[1]], std::get<1>(join_nodes.back()->input_ports()));
      
      auto new_merge_node = new merge_node(g, tbb::flow::unlimited, MERGE_STATE(d, od));
      merge_nodes.push_back(new_merge_node);
      
      tbb::flow::make_edge(*join_nodes.back(), *merge_nodes.back());
      
      tbb::flow::make_edge(*merge_nodes.back(), *state_nodes[ ances[i] ]);
    }
    
    state_vec output;
    tbb::flow::function_node< state_vec, state_vec> collect( g, tbb::flow::serial, collect_ll(output) );
    tbb::flow::make_edge(*merge_nodes.back(), collect);
    
    state_vec nodeM;
    connections = find_connections(for_time, ances.back());
    tbb::flow::function_node< state_vec, state_vec> collect_nodeM( g, tbb::flow::serial, collect_ll(nodeM) );
    tbb::flow::make_edge(*state_nodes[connections[1]], collect_nodeM);
    
  //  Rcpp::Rcout << "graph is setup\n"; force_output();
    
    
    for (size_t i = 0; i < num_tips; ++i) {
      tbb::flow::broadcast_node< state_vec > input(g);
      
      tbb::flow::make_edge(input, *state_nodes[i]);
      
      std::vector<double> startvec = states[i];
      startvec.push_back(0.0);

      input.try_put(startvec);
    }  
    
    g.wait_for_all();

  //  Rcpp::Rcout << "graph fully ran through\n"; force_output();
    
    double loglikelihood = output.back();
    
    NumericVector mergeBranch;
    for (int i = d; i < (d + d); ++i) {
      mergeBranch.push_back(output[d]);
    }
    nodeM.pop_back();
    
    return Rcpp::List::create(Rcpp::Named("mergeBranch") = mergeBranch,
                              Rcpp::Named("nodeM") = nodeM,
                              Rcpp::Named("loglik") = loglikelihood);
  }
};



#endif
/*
 * Copyright 2014, IST Austria
 *
 * This file is part of TARA.
 *
 * TARA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TARA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with TARA.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "program/program.h"
#include "output_exception.h"
#include "barrier_synthesis.h"
#include "cssa/wmm.h"
#include "api/output/output_base_utilities.h"
#include <chrono>
#include <algorithm>

using namespace tara;
using namespace tara::cssa;
using namespace tara::api::output;
using namespace tara::helpers;
using namespace std;

namespace tara {
namespace api {
namespace output {

ostream& operator<<( ostream& stream, const cycle& c ) {
  stream << c.name << "(";
  bool first = true;
  for( const auto& edge : c.edges ) {
    if(first) {
      if( edge.before )
        stream << edge.before->name();
      else
        stream << "null";
      first = false;
    }
    switch( edge.type ) {
    case edge_type::hb:  {stream << "->";} break;
    case edge_type::ppo: {stream << "=>";} break;
    case edge_type::rpo: {stream << "~~>";} break;
    }
    if( edge.after )
      stream << edge.after->name();
    else
      stream << "null";
  }
  stream << ")";
  return stream;
}

// ostream& operator<<(ostream& stream, const barrier& barrier)
// {
//   // stream << barrier.name << "(";
//   // for(auto l = barrier.locations.begin(); l!=barrier.locations.end(); l++) {
//   //   stream << *l ;
//   //   if (!last_element(l, barrier.locations)) stream << ", ";
//   // }
//   // stream << ")";
//   // return stream;
// }

}}}

cycle_edge::cycle_edge( hb_enc::se_ptr _before, hb_enc::se_ptr _after, edge_type _type )
    :before(_before),after(_after),type(_type) {}


cycle::cycle( cycle& _c, unsigned i ) {
  edges = _c.edges;
  relaxed_edges = _c.relaxed_edges;
  remove_prefix(i);
  assert( first() == last() );
}

void cycle::remove_prefix( unsigned i ) {
  assert( i <= edges.size() );
  edges.erase( edges.begin(), edges.begin()+i );
}

void cycle::remove_suffix( unsigned i ) {
  assert( i <= edges.size() );
  edges.erase( edges.end()-i, edges.end() );
}

void cycle::remove_prefix( hb_enc::se_ptr e ) {
  unsigned i = 0;
  for(; i < edges.size(); i++ ) {
    if( edges[i].before == e ) break;
  }
  remove_prefix(i);
}

void cycle::remove_suffix( hb_enc::se_ptr e ) {
  unsigned i = 0;
  for(; i < edges.size(); i++ ) {
    if( edges[i].after == e ) break;
  }
  remove_suffix(i+1);
}

unsigned cycle::has_cycle() {
  unsigned i = 0;
  hb_enc::se_ptr e = last();
  for(; i < edges.size(); i++ ) {
    if( edges[i].before == e ) break;
  }
  return i;
}

void cycle::pop() {
  auto& edge = edges.back();
  if( edge.type ==  edge_type::rpo ) {
    assert( !relaxed_edges.empty() );
    relaxed_edges.pop_back();
  }
  edges.pop_back();
}

void cycle::clear() {
  edges.clear();
  closed = false;
}

void cycle::close() {
  hb_enc::se_ptr l = last();
  remove_prefix( l );
  if( first() == l ) closed = true;
}

bool cycle::add_edge( hb_enc::se_ptr before, hb_enc::se_ptr after, edge_type t ) {
  if( !closed && last() == before ) {
    cycle_edge ed(before, after, t);
    edges.push_back(ed);
    if( t == edge_type::rpo ) {
      relaxed_edges.push_back( ed );
    }
  }else{
    // throw error
    assert(false);
  }
  // close();
  return closed;
}

bool cycle::add_edge( hb_enc::se_ptr after, edge_type t ) {
  return add_edge( last(), after, t );
}

bool cycle::has_relaxed( cycle_edge& edge) {
  auto it = std::find( relaxed_edges.begin(), relaxed_edges.end(), edge);
  return it != relaxed_edges.end();
}

//I am dominated
bool cycle::is_dominated_by( cycle& c ) {
  for( auto& edge : c.relaxed_edges  ) {
    if( !has_relaxed( edge ) ) return false;
  }
  //all relaxed edges are in c
  // std::cerr << *this << " is dominated by\n"<< c << "\n";
  return true;
}

//----------------------------------------------------------------------------

barrier_synthesis::barrier_synthesis(helpers::z3interf& z3_,
                                     bool verify, bool _verbose)
  : output_base( z3_), verbose(_verbose)
  , normal_form(z3_, true, false, false, false, verify)
{}

void barrier_synthesis::init( const hb_enc::encoding& hb_encoding,
                              const z3::solver& sol_desired,
                              const z3::solver& sol_undesired,
                              std::shared_ptr< const tara::program > _program )
{
    output_base::init(hb_encoding, sol_desired, sol_undesired, _program);
    normal_form.init(hb_encoding, sol_desired, sol_undesired, _program);
    // program = _program;
}


edge_type barrier_synthesis::is_ppo( hb_enc::se_ptr before,
                                     hb_enc::se_ptr after ) {

  assert( before );
  assert( after );
  assert( before->is_rd() || before->is_wr() );
  assert( after->is_rd() || after->is_wr() );
  assert( before->tid == after->tid );
  if( !program->is_mm_sc() && !program->is_mm_tso() &&
      !program->is_mm_pso() && !program->is_mm_rmo() )
    program->unsupported_mm();
  unsigned b_num = before->get_instr_no();
  unsigned a_num = after->get_instr_no();
  assert( b_num <= a_num);
  if( a_num == b_num ) {
    if( before->is_rd() && after->is_wr() ) return edge_type::ppo;
    return edge_type::rpo;
  }
  if( program->is_original() ) {
    auto p = (cssa::program*)(program.get());
    if( p->has_barrier_in_range( before->tid, b_num, a_num ) )
      return edge_type::ppo;
  }else{
    barrier_synthesis_error( "new version of program not supported!!" );
  }

  if( program->is_mm_sc() ) {
    return edge_type::ppo;
  }else if( program->is_mm_tso() ) {
    if( before->is_wr() && after->is_rd() ) return edge_type::rpo;
    return edge_type::ppo;
  }else if( program->is_mm_pso() ) {
    if( before->is_rd() ) return edge_type::ppo;
    if( before->is_wr() && after->is_rd() ) return edge_type::rpo;
    if( before->prog_v == after->prog_v ) return edge_type::ppo;
    return edge_type::rpo;
  }else if( program->is_mm_rmo() ) {
    if( after->is_rd() ) return edge_type::rpo;
    if( before->prog_v == after->prog_v ) return edge_type::ppo;
    if( before->is_wr() ) return edge_type::rpo;
    auto& deps = after->data_dependency;//.at(after);
    for( auto& dep : deps ) {
      if( dep.e == before ) {
        assert(false);
        //todo : check conditional depedency!!!
        // if( dep.cond != hb_encoding.ctx.bool_val(true) ) {
        //   barrier_synthesis_error( "conditional dependency not supported!!" );
        // }
        return edge_type::ppo;
      }
      // if( deps.find(before) != deps.end() ) return edge_type::ppo;
    }
    return edge_type::rpo;
  }else{
    program->unsupported_mm();
  }
  return edge_type::ppo;
}

void barrier_synthesis::insert_event( vector<hb_enc::se_vec>& event_lists,
                                      hb_enc::se_ptr e ) {
  unsigned i_no = e->get_instr_no();

  hb_enc::se_vec& es = event_lists[e->tid];
  auto it = es.begin();
  for(; it < es.end() ;it++) {
    hb_enc::se_ptr& e1 = *it;
    if( e1 == e ) return;
    if( e1->get_instr_no() > i_no ||
        ( e1->get_instr_no() == i_no && e1->is_wr() && e->is_rd() ) ) {
      break;
    }
  }
  es.insert( it, e);
}

//----------------------------------------------------------------------------
// cycle detection

void barrier_synthesis::succ( hb_enc::se_ptr e,
                              const hb_conj& hbs,
                              const vector<hb_enc::se_vec>& event_lists,
                              const set<hb_enc::se_ptr>& filter,
                              vector< pair< hb_enc::se_ptr, edge_type> >& next_set ) {
  for( auto it : hbs ) {
    // auto hb_from_b = *it;
    if( it.first == e ) {
      if( filter.find( it.second ) == filter.end() ) continue;
      next_set.push_back( {it.second,edge_type::hb} );
    }
  }
  const hb_enc::depends_set after = program->may_after.at(e);
  for(std::set<hb_enc::depends>::iterator it = after.begin(); it != after.end(); it++) {
    hb_enc::depends dep = *it;
    z3::expr cond = dep.cond;
    if( filter.find( e ) != filter.end() ) continue;
    if( dep.cond ) next_set.push_back( {dep.e, edge_type::ppo } );
    else next_set.push_back( {dep.e, edge_type::rpo } );
  }
    // break; // todo <- do we miss anything
}


void barrier_synthesis::find_sccs_rec( hb_enc::se_ptr e,
                                       const hb_conj& hbs,
                                       const vector<hb_enc::se_vec>& event_lists,
                                       const set<hb_enc::se_ptr>& filter,
                                       vector< set<hb_enc::se_ptr> >& sccs ) {
  index_map[e] = scc_index;
  lowlink_map[e] = scc_index;
  on_stack[e] = true;
  scc_index = scc_index + 1;
  scc_stack.push_back(e);
  vector< pair< hb_enc::se_ptr, edge_type> > next_set;
  succ( e, hbs, event_lists, filter, next_set );
  for( auto& ep_pair :  next_set ) {
    hb_enc::se_ptr ep = ep_pair.first;
    if( index_map.find(ep) == index_map.end() ) {
      find_sccs_rec( ep, hbs, event_lists, filter, sccs );
      lowlink_map[e] = std::min( lowlink_map.at(e), lowlink_map.at(ep) );
    }else if( on_stack.at(ep) ) {
      lowlink_map.at(e) = std::min( lowlink_map.at(e), index_map.at(ep) );
    }
  }
  if( lowlink_map.at(e) == index_map.at(e) ) {
    // pop to collect its members
    set<hb_enc::se_ptr> scc;
    hb_enc::se_ptr ep;
    do{
      ep = scc_stack.back();
      scc_stack.pop_back();
      on_stack.at(ep) = false;
      scc.insert( ep );
    }while( ep != e);
    if( scc.size() > 1 ) sccs.push_back( scc );
  }
}

void barrier_synthesis::find_sccs(  const hb_conj& hbs,
                                    const vector<hb_enc::se_vec>& event_lists,
                                    const set<hb_enc::se_ptr>& filter,
                                    vector< set<hb_enc::se_ptr> >& sccs ) {
  index_map.clear();
  lowlink_map.clear();
  on_stack.clear();
  scc_index = 0;
  //assert( hbs.size() > 0 );
  while(1) {
    hb_enc::se_ptr e;
    for( const auto hb : hbs ){
      if( index_map.find( hb.first ) == index_map.end() ) { e = hb.first; break; }
      if( index_map.find( hb.second ) == index_map.end() ) { e = hb.second; break; }
    }
    if( e ) {
      find_sccs_rec( e, hbs, event_lists, filter, sccs );
    }else{
      break;
    }
  }

  // if( verbose ) {
  //   auto& stream = std::cout;
  //   stream << "scc detected:\n";
  //   for( auto& scc : sccs ) {
  //     for( auto e : scc )
  //       stream << e->name() << " ";
  //     stream << endl;
  //   }
  // }
}


void barrier_synthesis::cycles_unblock( hb_enc::se_ptr e ) {
  blocked[e] = false;
  hb_enc::se_set e_set = B_map.at(e);
  B_map.at(e).clear();
  for( hb_enc::se_ptr ep : e_set ) {
    if( blocked.at( ep) )
      cycles_unblock(ep);
  }
}

bool barrier_synthesis::is_relaxed_dominated( cycle& c ,
                                              vector<cycle>& cs ) {
  for( cycle& cp : cs ) {
    if( c.is_dominated_by(cp) ) {
      return true;
    }
  }
  return false;
}

bool barrier_synthesis::find_true_cycles_rec( hb_enc::se_ptr e,
                                              const hb_conj& hbs,
                                              const vector<hb_enc::se_vec>& event_lists,
                                              const set<hb_enc::se_ptr>& scc,
                                              vector<cycle>& found_cycles ) {
  bool f = false;
  blocked[e] = true;
  vector< pair< hb_enc::se_ptr, edge_type> > next_set;
  succ( e, hbs, event_lists, scc, next_set );
  for( auto& ep_pair :  next_set ) {
    hb_enc::se_ptr ep = ep_pair.first;
    if( ep == root ) {
      ancestor_stack.add_edge( ep, ep_pair.second );
      if( ep_pair.second != edge_type::rpo ||
          !is_relaxed_dominated( ancestor_stack , found_cycles ) ) {
        cycle c( ancestor_stack, ancestor_stack.has_cycle()); // 0 or 1??
        for( auto it = found_cycles.begin(); it != found_cycles.end();) {
          if( it->is_dominated_by( c ) ) {
            it = found_cycles.erase( it );
          }else{
            it++;
          }
        }
        found_cycles.push_back(c);
      }
      ancestor_stack.pop();
      f = true;
    }else if( !blocked[ep] ) {
      ancestor_stack.add_edge( ep, ep_pair.second );
      if( ep_pair.second != edge_type::rpo ||
          !is_relaxed_dominated( ancestor_stack , found_cycles ) ) {
        bool fp = find_true_cycles_rec( ep, hbs, event_lists, scc, found_cycles );
        if( fp ) f = true;
      }
    }
  }
  if( f ) {
    cycles_unblock( e );
  }else{
    for( auto& ep_pair :  next_set ) {
      hb_enc::se_ptr ep = ep_pair.first;
      // assert( B_map[ep].empty() )
      B_map[ep].insert(e);
    }
  }
  ancestor_stack.pop();
  return f;
}

void barrier_synthesis::find_true_cycles( hb_enc::se_ptr e,
                                          const hb_conj& hbs,
                                          const vector<hb_enc::se_vec>& event_lists,
                                          const set<hb_enc::se_ptr>& scc,
                                          vector<cycle>& found_cycles ) {
  ancestor_stack.clear();
  ancestor_stack.add_edge(e,edge_type::hb);
  root = e;
  B_map.clear();
  blocked.clear();
  for( hb_enc::se_ptr ep : scc ) {
    B_map[ep].clear();
    blocked[ep] = false;
  }
  find_true_cycles_rec( e, hbs, event_lists, scc, found_cycles );
}

void barrier_synthesis::find_cycles_internal( hb_conj& hbs,
                                              vector<hb_enc::se_vec>& event_lists,
                                              set<hb_enc::se_ptr>& all_events,
                                              vector<cycle>& found_cycles ) {

  if(1){ // New implementation
    vector< set<hb_enc::se_ptr> > sccs;
    find_sccs( hbs, event_lists, all_events, sccs );

    while( !sccs.empty() ) {
      auto scc = sccs.back();
      sccs.pop_back();
      if( scc.size() > 1 ) {
        hb_enc::se_ptr e = *scc.begin();
        find_true_cycles( e, hbs, event_lists, scc, found_cycles );
        scc.erase(e);
        find_sccs( hbs, event_lists, scc, sccs );
      }
    }

  }else{
  while( !hbs.empty() ) {
      auto hb= hbs[0];
      hb_enc::se_ptr b = hb.first;
      // hb_enc::se_ptr a = hb->second;
      set<hb_enc::se_ptr> explored;
      cycle ancestor; // current candidate cycle
      std::vector< pair<hb_enc::se_ptr,edge_type> > stack;
      stack.push_back({b,edge_type::hb});
      while( !stack.empty() ) {
        pair<hb_enc::se_ptr,edge_type> pair = stack.back();
        hb_enc::se_ptr b = pair.first;
        edge_type type = pair.second;
        if( explored.find(b) != explored.end() ) {
          stack.pop_back();
          // explored
        }else if( ancestor.last() == b ) {
          // subtree has been explored; now I am also explored
          explored.insert(b);
          stack.pop_back();
          ancestor.pop();
        }else{
          ancestor.add_edge(b,type);
          unsigned stem_len = ancestor.has_cycle();
          if( stem_len != ancestor.size() ) {
            //cycle detected
            cycle c(ancestor, stem_len);
            found_cycles.push_back(c);
          }else{
            // Further expansion
            for( auto it = hbs.begin(); it != hbs.end(); ) {
              auto hb_from_b = *it;
              if( hb_from_b.first == b ) {
                hb_enc::se_ptr a = hb_from_b.second;
                stack.push_back( {a,edge_type::hb} );
                it = hbs.erase(it);
              }else{
                it++;
              }
            }
            hb_enc::se_vec& es = event_lists[b->tid];
            auto it = es.begin();
            for(;it < es.end();it++) {
              if( *it == b ) break;
            }
            for(;it < es.end();it++) {
              hb_enc::se_ptr a = *it;
              if( a->get_instr_no() != b->get_instr_no() ) break;
              if( a->is_wr() && b->is_rd() ) break;
            }
            for(;it < es.end();it++) {
              hb_enc::se_ptr a = *it;
              stack.push_back( {a, is_ppo(b, a) });
            }
          }
        }
      }
    }
  }
}

// typedef  vector< hb_conj > se_cnf;
//todo: not detecting all cycles??
void barrier_synthesis::find_cycles(nf::result_type& bad_dnf) {
  all_cycles.clear();
  all_cycles.resize( bad_dnf.size() );
  unsigned bad_dnf_num = 0;
  for( auto c : bad_dnf ) {
    hb_conj hbs;
    vector<hb_enc::se_vec> event_lists;
    set<hb_enc::se_ptr> all_events;
    event_lists.resize( program->size() );

    for( tara::hb_enc::hb& h : c ) {
      hb_enc::se_ptr b = program->se_store.at( h.loc1->name );
      hb_enc::se_ptr a = program->se_store.at( h.loc2->name );
      hbs.push_back({b,a});
      insert_event( event_lists, b);
      insert_event( event_lists, a);
      all_events.insert( b );
      all_events.insert( a );
    }

    vector<cycle>& cycles = all_cycles[bad_dnf_num++];
    find_cycles_internal( hbs, event_lists, all_events, cycles);
    if( cycles.size() == 0 ) {
      throw output_exception( "barrier synthesis: a conjunct without any cycles!!");
    }
  }
}

void barrier_synthesis::print_all_cycles( ostream& stream ) const {
  stream << "cycles found!\n";
  for( auto& cycles : all_cycles ) {
    stream << "[" << endl;
    for( auto& cycle : cycles ) {
      stream << cycle << endl;
    }
    stream << "]" << endl;
  }
}

void barrier_synthesis::print(ostream& stream, bool machine_readable) const {
  if( verbose && !machine_readable )
    normal_form.print(stream, false);

  if( verbose ) {
    stream << "Detected cycles:\n";
    print_all_cycles( stream );
  }

  stream <<"Barriers must be inserted after the following instructions:- \n";
  for ( unsigned i = 0; i < barrier_where.size(); i++ ) {
    stream << "thread " << barrier_where[i]->get_tid() << ",instr "
           <<  barrier_where[i]->loc_name << endl;
  }

  stream << endl;
}

void barrier_synthesis::gather_statistics(api::metric& metric) const
{
  metric.additional_time = time;
  metric.additional_info = info;
}


//----------------------------------------------------------------------------

bool cmp( hb_enc::se_ptr a, hb_enc::se_ptr b ) {
  return ( a->get_instr_no() < b->get_instr_no() );
}


void barrier_synthesis::gen_max_sat_problem() {
  z3::context& z3_ctx = sol_bad->ctx();

  for( auto& cycles : all_cycles ) {
      for( auto& cycle : cycles ) {
        bool found = false;
        for( auto edge : cycle.edges ) {
          if( edge.type==edge_type::rpo ) {
            //check each edge for rpo: push them in another vector
            //compare the edges in same thread
            tid_to_se_ptr[ edge.before->tid ].push_back( edge.before );
            tid_to_se_ptr[ edge.before->tid ].push_back( edge.after );
            found = true;
          }
        }
        if( !found )
          throw output_exception( "cycles found without any relaxed program oders!!");
      }
  }

  for( auto it = tid_to_se_ptr.begin();  it != tid_to_se_ptr.end(); it++ ) {
    hb_enc::se_vec& vec = it->second;
    std::sort( vec.begin(), vec.end(), cmp );
    vec.erase( std::unique( vec.begin(), vec.end() ), vec.end() );
    for(auto e : vec ) {
      z3::expr s = z3.get_fresh_bool();
      // std::cout << s << e->name() << endl;
      segment_map.insert( {e,s} );
      soft.push_back( !s);
    }
  }

  z3::expr hard = z3_ctx.bool_val(true);
  for( auto& cycles : all_cycles ) {
    if( cycles.size() == 0 ) continue; // throw error unfixable situation
    z3::expr c_disjunct = z3_ctx.bool_val(false);
    for( auto& cycle : cycles ) {
      z3::expr r_conjunct = z3_ctx.bool_val(true);
      for( auto edge : cycle.edges ) {
        if( edge.type==edge_type::rpo ) {
          unsigned tid = edge.before->tid;
          auto& events = tid_to_se_ptr.at(tid);
          bool in_range = false;
          z3::expr s_disjunct = z3_ctx.bool_val(false);
          for( auto e : events ) {
            if( e == edge.before ) in_range = true;
            if( e == edge.after ) break;
            if( in_range ) {
              auto find_z3 = segment_map.find( e );
              s_disjunct = s_disjunct || find_z3->second;
            }
          }
          r_conjunct = r_conjunct && s_disjunct;
        }
      }
      z3::expr c = z3.get_fresh_bool();
      // z3_to_cycle.insert({c, &cycle});
      c_disjunct=c_disjunct || c;
      hard = hard && implies( c, r_conjunct );
    }
    hard = hard && c_disjunct;
  }
  cut.push_back( hard );

}

// we need to choose
// constraints for mk_edge( b, a)
// h_bij means that b~>i~>j and b,j is ordered at i
// h_bbb => true
// h_bij => /\_{k \in prev(i)} ( h_bkj \/
//                               ( h_bkk /\ ord(k,j) ) \/
//                               ( l_k /\ w_bk /\ is_write(j) ) \/
//                               b_k )
// w_bb => is_write(b)
// w_bi => (/\_{k \in prev(i)} (w_bk)) \/ (h_bii /\ is_write(i) )
// b_k => l_k
// return h_baa


z3::expr barrier_synthesis::get_h_var_bit( hb_enc::se_ptr b,
                                           hb_enc::se_ptr e_i,
                                           hb_enc::se_ptr e_j ) {
  auto pr = std::make_tuple( b, e_i, e_j );
  auto it = hist_map.find( pr );
  if( it != hist_map.end() )
    return it->second;
  z3::expr bit = z3.get_fresh_bool();
  hist_map.insert( std::make_pair( pr, bit) );
  return bit;
}

z3::expr barrier_synthesis::get_write_order_bit( hb_enc::se_ptr b,
                                                 hb_enc::se_ptr e ) {
  auto pr = std::make_pair( b, e);
  auto it = wr_ord_map.find( pr );
  if( it != wr_ord_map.end() )
    return it->second;
  z3::expr bit = z3.get_fresh_bool();
  wr_ord_map.insert( std::make_pair( pr, bit) );
  return bit;
}

z3::expr barrier_synthesis::get_barr_bit( hb_enc::se_ptr e ) {
  return barrier_map.at( e );
}

z3::expr barrier_synthesis::get_lw_barr_bit( hb_enc::se_ptr e ) {
  return light_barrier_map.at( e );
}


z3::expr barrier_synthesis::mk_edge_constraint( hb_enc::se_ptr b,
                                                hb_enc::se_ptr a,
                                                z3::expr& hard ) {
  mm_t mm = program->get_mm();
  hb_enc::se_tord_set pending;
  hb_enc::se_vec found;
  std::map< std::tuple<hb_enc::se_ptr,hb_enc::se_ptr,hb_enc::se_ptr>,z3::expr >
    history_map;
  std::map< hb_enc::se_ptr, z3::expr > forced_map;

  pending.insert( b );
  while( !pending.empty() ) {
    hb_enc::se_ptr e = helpers::pick( pending );
    found.push_back( e );
    if( e->get_topological_order() > a->get_topological_order() )
      continue;
    if( a == e ) break;
    for( auto& xpp : e->post_events ) {
      if( !helpers::exists( found, xpp.e ) ) pending.insert( xpp.e );
    }
  }

  for( auto it = found.begin(); it != found.end();it++ ) {
    hb_enc::se_ptr& i = *it;
    auto& it2 = it;
    for( it2++; it2 != found.end() ; it2++ ) {
      hb_enc::se_ptr& j = *it2;
      z3::expr conj = z3.mk_true();
      for( const hb_enc::se_ptr& k : i->prev_events ) {
        if( helpers::exists( found, k )  ) {
          z3::expr h_bkj = get_h_var_bit( b, k, j );
          z3::expr h_bkk = cssa::wmm_event_cons::check_ppo(mm,k,j) ?
            get_h_var_bit(b,k,k):z3.mk_false();
          // todo: check for the lwsync requirement
          z3::expr lw_k = j->is_wr()? get_lw_barr_bit( k ) : z3.mk_false();
          lw_k = lw_k && get_write_order_bit( b, k );
          z3::expr b_k = get_barr_bit( k );
          conj = conj && (h_bkj || h_bkk || lw_k || b_k);
        }
      }
      z3::expr h_bij = get_h_var_bit( b, i, j );;
      hard = hard && implies( h_bij, conj );
    }
    z3::expr conj = z3.mk_true();
    for( const hb_enc::se_ptr& k : i->prev_events ) {
      if( helpers::exists( found, k ) ) {
        z3::expr w_bk = get_write_order_bit( b, k );
        conj = conj && w_bk;
      }
    }
    // todo: check for the lwsync requirement
    z3::expr h_bii = i->is_wr() ? get_h_var_bit( b, i, i ) : z3.mk_false();
    z3::expr w_bi = get_write_order_bit( b, i );
    hard = implies( w_bi, conj ||  h_bii );
  }

  // pending.insert( before );
  // while( !pending.empty() ) {
  //   hb_enc::se_ptr e = helpers::pick_and_move( pending, visited );

  //   if( e->get_topological_order() > after->get_topological_order() )
  //     continue;

  //   z3::expr h = z3.get_fresh_bool("h");
  //   std::make_triple<b,>
  //   history_map.insert( std::make_pair( {b, }, h ) );
  //   // z3::expr f = z3.get_fresh_bool("f");
  //   // forced_map.insert( std::make_pair( e, f ) );
  //   z3::expr conj =  z3.mk_true();
  //   for( const hb_enc::se_ptr& ep : e->prev_events ) {
  //     if( history_map.find( ep ) != history_map.end() ) {
  //       conj = conj && history_map.at(ep);
  //     }
  //   }

  //   if( after == e ) {
  //     hard = hard && implies( h, conj );
  //     return h;
  //   }else{
  //     hard = hard && implies( h, ( conj || event_bit_map.at(e) ) );
  //   }
  //   for( auto& xpp : e->post_events ) {
  //     if( !helpers::exists( visited, xpp.e ) ) pending.insert( xpp.e );
  //   }
  // }
  assert( false );
  return z3.mk_false(); //todo : may be reachable for unreachable pairs
}

void barrier_synthesis::gen_max_sat_problem_new() {
  z3::context& z3_ctx = sol_bad->ctx();

  for( unsigned t = 0; t < program->size(); t++ ) {
     const auto& thread = program->get_thread( t );
     for( const auto& e : thread.events ) {
       z3::expr h = z3.get_fresh_bool();
       barrier_map.insert({ e, h });
       light_barrier_map.insert({ e, h });
       soft.push_back( !h );
     }
  }

  z3::expr hard = z3.mk_true();
  for( auto& cycles : all_cycles ) {
    if( cycles.size() == 0 ) continue; // throw error unfixable situation
    z3::expr c_disjunct = z3_ctx.bool_val(false);
    for( auto& cycle : cycles ) {
      z3::expr r_conjunct = z3.mk_true();
      for( auto edge : cycle.edges ) {
        if( edge.type==edge_type::rpo ) {
          z3::expr s_disjunct = z3_ctx.bool_val(false);
          s_disjunct = mk_edge_constraint( edge.before, edge.after, hard );
          r_conjunct = r_conjunct && s_disjunct;
        }
      }
      z3::expr c = z3.get_fresh_bool();
      c_disjunct = c_disjunct || c;
      hard = hard && implies( c, r_conjunct );
    }
    hard = hard && c_disjunct;
  }
  cut.push_back( hard );

}

// ===========================================================================
// max sat code

void barrier_synthesis::assert_soft_constraints( z3::solver&s ,
                                                 z3::context& ctx,
                                                 std::vector<z3::expr>& cnstrs,
                                                 std::vector<z3::expr>& aux_vars
                                                 ) {
  for( auto f : cnstrs ) {
    auto n = z3.get_fresh_bool();
    aux_vars.push_back(n);
    s.add( f || n ) ;
  }
}

z3::expr barrier_synthesis:: at_most_one( z3::expr_vector& vars ) {
  z3::expr result = vars.ctx().bool_val(true);
  if( vars.size() <= 1 ) return result;
  // todo check for size 0
  z3::expr last_xor = vars[0];
  
  for( unsigned i = 1; i < vars.size(); i++ ) {
    z3::expr curr_xor = (vars[i] != last_xor );
    result = result && (!last_xor || curr_xor);
    last_xor = curr_xor;
  }

  return result;
}

int barrier_synthesis:: fu_malik_maxsat_step( z3::solver &s,
                          z3::context &ctx,
                          std::vector<z3::expr>& soft_cnstrs,
                          std::vector<z3::expr>& aux_vars ) {
    z3::expr_vector assumptions(ctx);
    z3::expr_vector core(ctx);
    for (unsigned i = 0; i < soft_cnstrs.size(); i++) {
      assumptions.push_back(!aux_vars[i]);
    }
    if (s.check(assumptions) != z3::check_result::unsat) {
      return 1; // done
    }else {
      core=s.unsat_core();
      z3::expr_vector block_vars(ctx);
      // update soft-constraints and aux_vars
      for (unsigned i = 0; i < soft_cnstrs.size(); i++) {
        unsigned j;
        // check whether assumption[i] is in the core or not
        for( j = 0; j < core.size(); j++ ) {
          if( assumptions[i] == core[j] )
            break;
        }
        if (j < core.size()) {
          z3::expr block_var = z3.get_fresh_bool();
          z3::expr new_aux_var = z3.get_fresh_bool();
          soft_cnstrs[i] = ( soft_cnstrs[i] || block_var );
          aux_vars[i]    = new_aux_var;
          block_vars.push_back( block_var );
          s.add( soft_cnstrs[i] || new_aux_var );
        }
      }
      z3::expr at_most_1 = at_most_one( block_vars );
      s.add( at_most_1 );
      return 0; // not done.
    }
}

z3::model
barrier_synthesis::fu_malik_maxsat( z3::context& ctx,
                                    z3::expr hard,
                                    std::vector<z3::expr>& soft_cnstrs ) {
    unsigned k;
    z3::solver s(ctx);
    s.add( hard );
    assert( s.check() != z3::unsat );
    assert( soft_cnstrs.size() > 0 );

    std::vector<z3::expr> aux_vars;
    assert_soft_constraints( s,ctx ,soft_cnstrs, aux_vars );
    k = 0;
    for (;;) {
      if( fu_malik_maxsat_step(s, ctx, soft_cnstrs, aux_vars) ) {
    	  z3::model m=s.get_model();
    	  return m;
      }
      k++;
    }
}

// ===========================================================================
// main function for synthesis

void barrier_synthesis::output(const z3::expr& output) {
    output_base::output(output);
    normal_form.output(output);
    
    nf::result_type bad_dnf = normal_form.get_result(true, true);

    // measure time
    auto start_time = chrono::steady_clock::now();

    find_cycles( bad_dnf );

    gen_max_sat_problem();

    if( verbose ) {
      std::cout << cut[0] << endl;
      std::cout << "soft:" << endl;
      for( auto s : soft ) {
        std::cout << s << endl;
      }
    }

    if( soft.size() ==  0 ) {
      throw output_exception( "No relaxed edge found in any cycle!!");
      return;
    }
    z3::model m =fu_malik_maxsat( sol_bad->ctx(), cut[0], soft );
    for( auto it=segment_map.begin(); it != segment_map.end(); it++ ) {
      hb_enc::se_ptr e = it->first;
      z3::expr b = it->second;
      if( m.eval(b).get_bool() )
        barrier_where.push_back( e );
    }

    info = to_string(all_cycles.size()) + " " +
      to_string(barrier_where.size()) + " ";

    auto delay = chrono::steady_clock::now() - start_time;
    time = chrono::duration_cast<chrono::microseconds>(delay).count();
}

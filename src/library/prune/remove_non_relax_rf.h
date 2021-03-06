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

#ifndef TARA_PRUNE_REMOVE_NON_RELAX_RF_H
#define TARA_PRUNE_REMOVE_NON_RELAX_RF_H

#include "prune/prune_base.h"

namespace tara {
namespace prune {
class remove_non_relax_rf : public prune::prune_base
{
public:
  remove_non_relax_rf(const helpers::z3interf& z3, const tara::program& program);
  virtual hb_enc::hb_vec prune( const hb_enc::hb_vec& hbs, const z3::model& m );

  virtual std::string name();
private:
  variable_set relaxed_wrs;
};
}}

#endif // PRUNE_REMOVE_NON_RELAX_RF_H

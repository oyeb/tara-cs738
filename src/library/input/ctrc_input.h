/*
 * Copyright 2017, TIFR
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
 */

#ifndef TARA_CTRCCINPUT_H
#define TARA_CTRC_INPUT_H

#include "helpers/z3interf.h"
#include "api/options.h"
#include "hb_enc/encoding.h"
#include "input/ctrc_program.h"

namespace tara {
  class program;
namespace ctrc_input {

  ctrc::program* parse_ctrc_file( helpers::z3interf& z3_, api::options& o_,
                                  hb_enc::encoding& hb_encoding,
                                  // input::program& pa,
                                  std::string& cfile );

}}
#endif // TARA_CTRC_INPUT_H

/*
 *  Generate code to convert between VHDL types.
 *
 *  Copyright (C) 2008  Nick Gasson (nick@nickg.me.uk)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "vhdl_syntax.hh"

#include "vhdl_target.h"
#include "support.hh"

#include <cassert>
#include <iostream>

vhdl_expr *vhdl_expr::cast(const vhdl_type *to)
{
   //std::cout << "Cast: from=" << type_->get_string()
   //          << " (" << type_->get_width() << ") "
   //          << " to=" << to->get_string() << " ("
   //          << to->get_width() << ")" << std::endl;
   
   if (to->get_name() == type_->get_name()) {
      if (to->get_width() == type_->get_width())
         return this;  // Identical
      else
         return resize(to->get_width());
   }
   else if (to->get_name() == VHDL_TYPE_BOOLEAN) {
      if (type_->get_name() == VHDL_TYPE_STD_LOGIC) {
         // '1' is true all else are false
         vhdl_const_bit *one = new vhdl_const_bit('1');
         return new vhdl_binop_expr
            (this, VHDL_BINOP_EQ, one, vhdl_type::boolean());
      }
      else if (type_->get_name() == VHDL_TYPE_UNSIGNED) {
         // Need to use a support function for this conversion
         require_support_function(SF_UNSIGNED_TO_BOOLEAN);

         vhdl_fcall *conv =
            new vhdl_fcall(support_function::function_name(SF_UNSIGNED_TO_BOOLEAN),
                           vhdl_type::boolean());
         conv->add_expr(this);
         return conv;
      }
      else if (type_->get_name() == VHDL_TYPE_SIGNED) {
         require_support_function(SF_SIGNED_TO_BOOLEAN);

         vhdl_fcall *conv =
            new vhdl_fcall(support_function::function_name(SF_SIGNED_TO_BOOLEAN),
                           vhdl_type::boolean());
         conv->add_expr(this);
         return conv;
      }
      else {
         assert(false);
      }
   }
   else if (to->get_name() == VHDL_TYPE_INTEGER) {
      vhdl_fcall *conv;
      if (type_->get_name() == VHDL_TYPE_STD_LOGIC) {
         require_support_function(SF_LOGIC_TO_INTEGER);
         conv = new vhdl_fcall(support_function::function_name(SF_LOGIC_TO_INTEGER),
                               vhdl_type::integer());
      }
      else
         conv = new vhdl_fcall("To_Integer", new vhdl_type(*to));
      
      conv->add_expr(this);

      return conv;
   }
   else if ((to->get_name() == VHDL_TYPE_UNSIGNED
             || to->get_name() == VHDL_TYPE_SIGNED
             || to->get_name() == VHDL_TYPE_STD_LOGIC_VECTOR) &&
            type_->get_name() == VHDL_TYPE_STD_LOGIC) {

      vhdl_expr *others = to->get_width() == 1 ? NULL : new vhdl_const_bit('0');
      vhdl_bit_spec_expr *bs =
         new vhdl_bit_spec_expr(new vhdl_type(*to), others);
      bs->add_bit(0, this);

      return bs;
   }
   else if (to->get_name() == VHDL_TYPE_STD_LOGIC &&
            type_->get_name() == VHDL_TYPE_BOOLEAN) {
      require_support_function(SF_BOOLEAN_TO_LOGIC);
      
      vhdl_fcall *ah =
         new vhdl_fcall(support_function::function_name(SF_BOOLEAN_TO_LOGIC),
                        vhdl_type::std_logic());
      ah->add_expr(this);

      return ah;
   }
   else {
      // We have to cast the expression before resizing or the
      // wrong sign bit may be extended (i.e. when casting between
      // signed/unsigned *and* resizing)
      vhdl_fcall *conv =
         new vhdl_fcall(to->get_string().c_str(), new vhdl_type(*to));
      conv->add_expr(this);

      if (to->get_width() != type_->get_width())
         return conv->resize(to->get_width());
      else
         return conv;      
   }
}

vhdl_expr *vhdl_expr::resize(int newwidth)
{
   vhdl_type *rtype;
   assert(type_);
   if (type_->get_name() == VHDL_TYPE_SIGNED)
      rtype = vhdl_type::nsigned(newwidth);
   else if (type_->get_name() == VHDL_TYPE_UNSIGNED)
      rtype = vhdl_type::nunsigned(newwidth);
   else
      return this;   // Doesn't make sense to resize non-vector type
   
   vhdl_fcall *resize = new vhdl_fcall("Resize", rtype);
   resize->add_expr(this);
   resize->add_expr(new vhdl_const_int(newwidth));
   
   return resize;
}

vhdl_expr *vhdl_const_int::cast(const vhdl_type *to)
{
   if (to->get_name() == VHDL_TYPE_SIGNED
       || to->get_name() == VHDL_TYPE_UNSIGNED) {

      const char *fname = to->get_name() == VHDL_TYPE_SIGNED
         ? "To_Signed" : "To_Unsigned";
      vhdl_fcall *conv = new vhdl_fcall(fname, new vhdl_type(*to));
      conv->add_expr(this);
      conv->add_expr(new vhdl_const_int(to->get_width()));

      return conv;
   }
   else
      return vhdl_expr::cast(to);
}

int vhdl_const_bits::bits_to_int() const
{   
   char msb = value_[value_.size() - 1];
   int result = 0, bit;
   for (int i = sizeof(int)*8 - 1; i >= 0; i--) {
      if (i > (int)value_.size() - 1)
         bit = msb == '1' ? 1 : 0;
      else
         bit = value_[i] == '1' ? 1 : 0;
      result = (result << 1) | bit;
   }

   return result;
}

vhdl_expr *vhdl_const_bits::cast(const vhdl_type *to)
{  
   if (to->get_name() == VHDL_TYPE_STD_LOGIC) {
      // VHDL won't let us cast directly between a vector and
      // a scalar type
      // But we don't need to here as we have the bits available

      // Take the least significant bit
      char lsb = value_[0];

      return new vhdl_const_bit(lsb);
   }
   else if (to->get_name() == VHDL_TYPE_STD_LOGIC_VECTOR) {
      // Don't need to do anything
      return this;
   }
   else if (to->get_name() == VHDL_TYPE_SIGNED
            || to->get_name() == VHDL_TYPE_UNSIGNED) {

      // Extend with sign bit
      value_.resize(to->get_width(), value_[0]);  
      return this;
   }
   else if (to->get_name() == VHDL_TYPE_INTEGER)
      return new vhdl_const_int(bits_to_int());
   else
      return vhdl_expr::cast(to);
}

vhdl_expr *vhdl_const_bit::cast(const vhdl_type *to)
{
   if (to->get_name() == VHDL_TYPE_INTEGER)
      return new vhdl_const_int(bit_ == '1' ? 1 : 0);
   else if (to->get_name() == VHDL_TYPE_BOOLEAN)
      return new vhdl_const_bool(bit_ == '1');
   else
      return vhdl_expr::cast(to);
}

/*
 *  VHDL code generation for expressions.
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

#include "vhdl_target.h"
#include "support.hh"

#include <iostream>
#include <cassert>
#include <cstring>


/*
 * Change the signedness of a vector.
 */
static vhdl_expr *change_signedness(vhdl_expr *e, bool issigned)
{
   int msb = e->get_type()->get_msb();
   int lsb = e->get_type()->get_lsb();
   vhdl_type u(issigned ? VHDL_TYPE_SIGNED : VHDL_TYPE_UNSIGNED, msb, lsb);
   
   return e->cast(&u);
}

/*
 * Convert a constant Verilog string to a constant VHDL string.
 */
static vhdl_expr *translate_string(ivl_expr_t e)
{   
   // TODO: May need to inspect or escape parts of this
   const char *str = ivl_expr_string(e);
   return new vhdl_const_string(str);
}

/*
 * A reference to a signal in an expression. It's assumed that the
 * signal has already been defined elsewhere.
 */
static vhdl_var_ref *translate_signal(ivl_expr_t e)
{
   ivl_signal_t sig = ivl_expr_signal(e);
   
   const vhdl_scope *scope = find_scope_for_signal(sig);
   assert(scope);

   const char *renamed = get_renamed_signal(sig).c_str();
   
   vhdl_decl *decl = scope->get_decl(renamed);
   assert(decl);

   // Can't generate a constant initialiser for this signal
   // later as it has already been read
   if (scope->initializing())
      decl->set_initial(NULL);

   vhdl_var_ref *ref =
      new vhdl_var_ref(renamed, new vhdl_type(*decl->get_type()));
      
   ivl_expr_t off;
   if (ivl_signal_array_count(sig) > 0 && (off = ivl_expr_oper1(e))) {
      // Select from an array
      vhdl_expr *vhd_off = translate_expr(off);
      if (NULL == vhd_off)
         return NULL;

      vhdl_type integer(VHDL_TYPE_INTEGER);
      ref->set_slice(vhd_off->cast(&integer));
   }

   return ref;
}

/*
 * A numeric literal ends up as std_logic bit string.
 */
static vhdl_expr *translate_number(ivl_expr_t e)
{
   if (ivl_expr_width(e) == 1)
      return new vhdl_const_bit(ivl_expr_bits(e)[0]);
   else
      return new vhdl_const_bits(ivl_expr_bits(e), ivl_expr_width(e),
                                 ivl_expr_signed(e) != 0);
}

static vhdl_expr *translate_ulong(ivl_expr_t e)
{
   return new vhdl_const_int(ivl_expr_uvalue(e));
}

static vhdl_expr *translate_reduction(support_function_t f, bool neg,
                                      vhdl_expr *operand)
{
   vhdl_expr *result;
   if (operand->get_type()->get_name() == VHDL_TYPE_STD_LOGIC)
      result = operand;
   else {
      require_support_function(f);
      vhdl_fcall *fcall =
         new vhdl_fcall(support_function::function_name(f),
                        vhdl_type::std_logic());
      
      vhdl_type std_logic_vector(VHDL_TYPE_STD_LOGIC_VECTOR);
      fcall->add_expr(operand->cast(&std_logic_vector));

      result = fcall;
   }

   if (neg)
      return new vhdl_unaryop_expr(VHDL_UNARYOP_NOT, result,
                                   vhdl_type::std_logic());
   else
      return result;
}

static vhdl_expr *translate_unary(ivl_expr_t e)
{
   vhdl_expr *operand = translate_expr(ivl_expr_oper1(e));
   if (NULL == operand)
      return NULL;

   bool should_be_signed = ivl_expr_signed(e) != 0;
   
   if (operand->get_type()->get_name() == VHDL_TYPE_UNSIGNED
       && should_be_signed) {
      //operand->print();
      //std::cout << "^ should be signed but is not" << std::endl;

      operand = change_signedness(operand, true);
   }
   else if (operand->get_type()->get_name() == VHDL_TYPE_SIGNED
            && !should_be_signed) {
      //operand->print();
      //std::cout << "^ should be unsigned but is not" << std::endl;

      operand = change_signedness(operand, false);
   }
   
   char opcode = ivl_expr_opcode(e);
   switch (opcode) {
   case '!':
   case '~':
      return new vhdl_unaryop_expr
         (VHDL_UNARYOP_NOT, operand, new vhdl_type(*operand->get_type()));
   case '-':
      operand = change_signedness(operand, true);
      return new vhdl_unaryop_expr
         (VHDL_UNARYOP_NEG, operand, new vhdl_type(*operand->get_type()));
   case 'N':   // NOR
      return translate_reduction(SF_REDUCE_OR, true, operand);
   case '|':
      return translate_reduction(SF_REDUCE_OR, false, operand);
   case 'A':   // NAND
      return translate_reduction(SF_REDUCE_AND, true, operand);
   case '&':
      return translate_reduction(SF_REDUCE_AND, false, operand);
   default:
      error("No translation for unary opcode '%c'\n",
            ivl_expr_opcode(e));
      delete operand;
      return NULL;
   }
}

/*
 * Translate a numeric binary operator (+, -, etc.) to
 * a VHDL equivalent using the numeric_std package.
 */
static vhdl_expr *translate_numeric(vhdl_expr *lhs, vhdl_expr *rhs,
                                    vhdl_binop_t op)
{
   // May need to make either side Boolean for operators
   // to work
   vhdl_type boolean(VHDL_TYPE_BOOLEAN);
   if (lhs->get_type()->get_name() == VHDL_TYPE_BOOLEAN)
      rhs = rhs->cast(&boolean);
   else if (rhs->get_type()->get_name() == VHDL_TYPE_BOOLEAN)
      lhs = lhs->cast(&boolean);

   vhdl_type *rtype = new vhdl_type(*lhs->get_type());
   return new vhdl_binop_expr(lhs, op, rhs, rtype);
}

static vhdl_expr *translate_relation(vhdl_expr *lhs, vhdl_expr *rhs,
                                     vhdl_binop_t op)
{
   // Generate any necessary casts
   // Arbitrarily, the RHS is casted to the type of the LHS
   vhdl_expr *r_cast = rhs->cast(lhs->get_type());
   
   return new vhdl_binop_expr(lhs, op, r_cast, vhdl_type::boolean());
}

/*
 * Like translate_relation but both operands must be Boolean.
 */
static vhdl_expr *translate_logical(vhdl_expr *lhs, vhdl_expr *rhs,
                                    vhdl_binop_t op)
{
   vhdl_type boolean(VHDL_TYPE_BOOLEAN);

   return translate_relation(lhs->cast(&boolean), rhs->cast(&boolean), op);
}

static vhdl_expr *translate_shift(vhdl_expr *lhs, vhdl_expr *rhs,
                                  vhdl_binop_t op)
{
   // The RHS must be an integer
   vhdl_type integer(VHDL_TYPE_INTEGER);
   vhdl_expr *r_cast = rhs->cast(&integer);

   vhdl_type *rtype = new vhdl_type(*lhs->get_type());
   return new vhdl_binop_expr(lhs, op, r_cast, rtype);
}

static vhdl_expr *translate_binary(ivl_expr_t e)
{
   vhdl_expr *lhs = translate_expr(ivl_expr_oper1(e));
   if (NULL == lhs)
      return NULL;
   
   vhdl_expr *rhs = translate_expr(ivl_expr_oper2(e));
   if (NULL == rhs)
      return NULL;

   int lwidth = lhs->get_type()->get_width();
   int rwidth = rhs->get_type()->get_width();
   int result_width = ivl_expr_width(e);
   
   // For === and !== we need to compare std_logic_vectors
   // rather than signeds
   vhdl_type std_logic_vector(VHDL_TYPE_STD_LOGIC_VECTOR, result_width-1, 0);
   vhdl_type_name_t ltype = lhs->get_type()->get_name();
   vhdl_type_name_t rtype = rhs->get_type()->get_name();
   bool vectorop =
      (ltype == VHDL_TYPE_SIGNED || ltype == VHDL_TYPE_UNSIGNED) &&
      (rtype == VHDL_TYPE_SIGNED || rtype == VHDL_TYPE_UNSIGNED);
   
   // May need to resize the left or right hand side
   if (vectorop) {
      if (lwidth < rwidth)
         lhs = lhs->resize(rwidth);
      else if (rwidth < lwidth)
         rhs = rhs->resize(lwidth);
   }

   vhdl_expr *result;
   switch (ivl_expr_opcode(e)) {
   case '+':
      result = translate_numeric(lhs, rhs, VHDL_BINOP_ADD);
      break;
   case '-':
      result = translate_numeric(lhs, rhs, VHDL_BINOP_SUB);
      break;
   case '*':
      result = translate_numeric(lhs, rhs, VHDL_BINOP_MULT);
      break;
   case 'e':
      result = translate_relation(lhs, rhs, VHDL_BINOP_EQ);
      break;
   case 'E':
      if (vectorop)
         result = translate_relation(lhs->cast(&std_logic_vector),
                                   rhs->cast(&std_logic_vector), VHDL_BINOP_EQ);
      else
         result = translate_relation(lhs, rhs, VHDL_BINOP_EQ);
      break;
   case 'n':
      result = translate_relation(lhs, rhs, VHDL_BINOP_NEQ);
      break;
   case 'N':
      if (vectorop)
         result = translate_relation(lhs->cast(&std_logic_vector),
                                   rhs->cast(&std_logic_vector), VHDL_BINOP_NEQ);
      else
         result = translate_relation(lhs, rhs, VHDL_BINOP_NEQ);
      break;
   case '&':    // Bitwise AND
      result = translate_numeric(lhs, rhs, VHDL_BINOP_AND);
      break;
   case 'a':    // Logical AND
      result = translate_logical(lhs, rhs, VHDL_BINOP_AND);
      break;
   case 'A':    // Bitwise NAND
      result = translate_numeric(lhs, rhs, VHDL_BINOP_NAND);
      break;
   case 'O':    // Bitwise NOR
      result = translate_numeric(lhs, rhs, VHDL_BINOP_NOR);
      break;
   case 'X':    // Bitwise XNOR
      result = translate_numeric(lhs, rhs, VHDL_BINOP_XNOR);
      break;
   case '|':    // Bitwise OR
      result = translate_numeric(lhs, rhs, VHDL_BINOP_OR);
      break;
   case 'o':    // Logical OR
      result = translate_logical(lhs, rhs, VHDL_BINOP_OR);
      break;
   case '<':
      result = translate_relation(lhs, rhs, VHDL_BINOP_LT);
      break;
   case 'L':
      result = translate_relation(lhs, rhs, VHDL_BINOP_LEQ);
      break;
   case '>':
      result = translate_relation(lhs, rhs, VHDL_BINOP_GT);
      break;
   case 'G':
      result = translate_relation(lhs, rhs, VHDL_BINOP_GEQ);
      break;
   case 'l':
      result = translate_shift(lhs, rhs, VHDL_BINOP_SL);
      break;
   case 'r':
      result = translate_shift(lhs, rhs, VHDL_BINOP_SR);
      break;
   case '^':
      result = translate_numeric(lhs, rhs, VHDL_BINOP_XOR);
      break;
   default:
      error("No translation for binary opcode '%c'\n",
            ivl_expr_opcode(e));
      delete lhs;
      delete rhs;
      return NULL;
   }

   if (NULL == result)
      return NULL;

   if (vectorop) {
      bool should_be_signed = ivl_expr_signed(e) != 0;

      if (result->get_type()->get_name() == VHDL_TYPE_UNSIGNED && should_be_signed) {
         //result->print();
         //std::cout << "^ should be signed but is not" << std::endl;

         result = change_signedness(result, true);
      }
      else if (result->get_type()->get_name() == VHDL_TYPE_SIGNED && !should_be_signed) {
         //result->print();
         //std::cout << "^ should be unsigned but is not" << std::endl;

         result = change_signedness(result, false);
      }

      int actual_width = result->get_type()->get_width();
      if (actual_width != result_width) {
         //result->print();
         //std::cout << "^ should be " << result_width << " but is " << actual_width << std::endl;
      }
   }

   return result;
}

static vhdl_expr *translate_select(ivl_expr_t e)
{
   vhdl_var_ref *from =
      dynamic_cast<vhdl_var_ref*>(translate_expr(ivl_expr_oper1(e)));
   if (NULL == from) {
      error("Can only select from variable reference");
      return NULL;
   }

   ivl_expr_t o2 = ivl_expr_oper2(e);
   if (o2) {
      vhdl_expr *base = translate_expr(ivl_expr_oper2(e));
      if (NULL == base)
         return NULL;

      vhdl_type integer(VHDL_TYPE_INTEGER);
      from->set_slice(base->cast(&integer), ivl_expr_width(e) - 1);
      return from;
   }
   else
      return from->resize(ivl_expr_width(e));
}

static vhdl_type *expr_to_vhdl_type(ivl_expr_t e)
{
   if (ivl_expr_signed(e))
      return vhdl_type::nsigned(ivl_expr_width(e));
   else
      return vhdl_type::nunsigned(ivl_expr_width(e));
}

template <class T>
static T *translate_parms(T *t, ivl_expr_t e)
{
   int nparams = ivl_expr_parms(e);
   for (int i = 0; i < nparams; i++) {
      vhdl_expr *param = translate_expr(ivl_expr_parm(e, i));
      if (NULL == param)
         return NULL;

      t->add_expr(param);
   }

   return t;
}

static vhdl_expr *translate_ufunc(ivl_expr_t e)
{
   ivl_scope_t defscope = ivl_expr_def(e);
   ivl_scope_t parentscope = ivl_scope_parent(defscope);
   assert(ivl_scope_type(parentscope) == IVL_SCT_MODULE);

   // A function is always declared in a module, which should have
   // a corresponding entity by this point: so we can get type
   // information, etc. from the declaration
   vhdl_entity *parent_ent = find_entity(ivl_scope_name(parentscope));
   assert(parent_ent);

   const char *funcname = ivl_scope_tname(defscope);

   vhdl_type *rettype = expr_to_vhdl_type(e);
   vhdl_fcall *fcall = new vhdl_fcall(funcname, rettype);

   return translate_parms<vhdl_fcall>(fcall, e);
}

static vhdl_expr *translate_ternary(ivl_expr_t e)
{
   support_function_t sf;
   int width = ivl_expr_width(e);
   bool issigned = ivl_expr_signed(e) != 0;
   if (width == 1)
      sf = SF_TERNARY_LOGIC;
   else if (issigned)
      sf = SF_TERNARY_SIGNED;
   else
      sf = SF_TERNARY_UNSIGNED;
   
   require_support_function(sf);

   vhdl_expr *test = translate_expr(ivl_expr_oper1(e));
   vhdl_expr *true_part = translate_expr(ivl_expr_oper2(e));
   vhdl_expr *false_part = translate_expr(ivl_expr_oper3(e));
   if (!test || !true_part || !false_part)
      return NULL;

   vhdl_type boolean(VHDL_TYPE_BOOLEAN);
   test = test->cast(&boolean);
   
   vhdl_fcall *fcall =
      new vhdl_fcall(support_function::function_name(sf),
                     vhdl_type::type_for(width, issigned));
   fcall->add_expr(test);
   fcall->add_expr(true_part);
   fcall->add_expr(false_part);
   
   return fcall;
}

static vhdl_expr *translate_concat(ivl_expr_t e)
{
   vhdl_type *rtype = expr_to_vhdl_type(e);
   vhdl_binop_expr *concat = new vhdl_binop_expr(VHDL_BINOP_CONCAT, rtype);

   int nrepeat = ivl_expr_repeat(e);
   while (nrepeat--)
      translate_parms<vhdl_binop_expr>(concat, e);

   return concat;
}

vhdl_expr *translate_sfunc_time(ivl_expr_t e)
{
   cerr << "warning: no translation for time (returning 0)" << endl;
   return new vhdl_const_int(0);
}

vhdl_expr *translate_sfunc(ivl_expr_t e)
{
   const char *name = ivl_expr_name(e);
   if (strcmp(name, "$time") == 0)
      return translate_sfunc_time(e);
   else {
      error("No translation for system function %s", name);
      return NULL;
   }
}

/*
 * Generate a VHDL expression from a Verilog expression.
 */
vhdl_expr *translate_expr(ivl_expr_t e)
{
   assert(e);
   ivl_expr_type_t type = ivl_expr_type(e);
   
   switch (type) {
   case IVL_EX_STRING:
      return translate_string(e);
   case IVL_EX_SIGNAL:
      return translate_signal(e);
   case IVL_EX_NUMBER:
      return translate_number(e);
   case IVL_EX_ULONG:
      return translate_ulong(e);
   case IVL_EX_UNARY:
      return translate_unary(e);
   case IVL_EX_BINARY:
      return translate_binary(e);
   case IVL_EX_SELECT:
      return translate_select(e);
   case IVL_EX_UFUNC:
      return translate_ufunc(e);
   case IVL_EX_TERNARY:
      return translate_ternary(e);
   case IVL_EX_CONCAT:
      return translate_concat(e);
   case IVL_EX_SFUNC:
      return translate_sfunc(e);
   default:
      error("No VHDL translation for expression at %s:%d (type = %d)",
            ivl_expr_file(e), ivl_expr_lineno(e), type);
      return NULL;
   }
}

/*
 * Translate an expression into a time. This is achieved simply
 * by multiplying the expression by 1ns.
 */
vhdl_expr *translate_time_expr(ivl_expr_t e)
{
   vhdl_expr *time = translate_expr(e);
   if (NULL == time)
      return NULL;

   vhdl_type integer(VHDL_TYPE_INTEGER);
   time = time->cast(&integer);
   
   vhdl_expr *ns1 = new vhdl_const_time(1, TIME_UNIT_NS);
   return new vhdl_binop_expr(time, VHDL_BINOP_MULT, ns1,
                              vhdl_type::time());
}

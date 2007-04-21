/*
 * Copyright (c) 2007 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#ifdef HAVE_CVS_IDENT
#ident "$Id: expression.c,v 1.6 2007/04/21 03:20:47 steve Exp $"
#endif

# include "config.h"
# include "priv.h"
# include <stdlib.h>
# include <inttypes.h>
# include <assert.h>

static const char*vt_type_string(ivl_expr_t net)
{
      return data_type_string(ivl_expr_value(net));
}

static void show_array_expression(ivl_expr_t net, unsigned ind)
{
      ivl_signal_t sig = ivl_expr_signal(net);
      const char*name = ivl_signal_basename(sig);
      unsigned width = ivl_signal_width(sig);
      const char*vt   = vt_type_string(net);

      fprintf(out, "%*sArray: %s, word_count=%u (%u dimensions), width=%u, type=%s\n",
	      ind, "", name, ivl_signal_array_count(sig),
	      ivl_signal_dimensions(sig), width, vt);
}

static void show_binary_expression(ivl_expr_t net, unsigned ind)
{
      unsigned width = ivl_expr_width(net);
      const char*sign = ivl_expr_signed(net)? "signed" : "unsigned";
      const char*vt   = vt_type_string(net);

      fprintf(out, "%*s<\"%c\" width=%u, %s, type=%s>\n", ind, "",
	      ivl_expr_opcode(net), width, sign, vt);
      show_expression(ivl_expr_oper1(net), ind+3);
      show_expression(ivl_expr_oper2(net), ind+3);

      switch (ivl_expr_opcode(net)) {

	  case '*':
	      /* The width of multiply expressions is the sum of the
		 widths of the operands. This is slightly different
		 from the way the Verilog standard does it, but allows
		 us to keep operands smaller. */
	    width = ivl_expr_width(ivl_expr_oper1(net));
	    width += ivl_expr_width(ivl_expr_oper2(net));
	    if (ivl_expr_width(net) != width) {
		  fprintf(out, "%*sERROR: Result width incorrect\n",
			  ind+3, "");
		  stub_errors += 1;
	    }
	    break;

	  default:
	    break;
      }
}

static void show_function_call(ivl_expr_t net, unsigned ind)
{
      ivl_scope_t def = ivl_expr_def(net);
      const char*sign = ivl_expr_signed(net)? "signed" : "unsigned";
      const char*vt = vt_type_string(net);

      fprintf(out, "%*s<%s %s function %s>\n", ind, "",
	      vt, sign, ivl_scope_name(def));
}

static void show_memory_expression(ivl_expr_t net, unsigned ind)
{
      unsigned width = ivl_expr_width(net);

      fprintf(out, "%*s<memory width=%u>\n", ind, "",
	      width);
}

static void show_signal_expression(ivl_expr_t net, unsigned ind)
{
      unsigned width = ivl_expr_width(net);
      const char*sign = ivl_expr_signed(net)? "signed" : "unsigned";
      const char*vt = vt_type_string(net);
      ivl_expr_t word = ivl_expr_oper1(net);

      ivl_signal_t sig = ivl_expr_signal(net);
      unsigned dimensions = ivl_signal_dimensions(sig);
      unsigned word_count = ivl_signal_array_count(sig);

      if (dimensions == 0 && word_count != 1) {
	    fprintf(out, "%*sERROR: Word count = %u for non-array object\n",
		    ind, "", word_count);
	    stub_errors += 1;
      }

      fprintf(out, "%*s<signal=%s, words=%u, width=%u, %s type=%s>\n", ind, "",
	      ivl_expr_name(net), word_count, width, sign, vt);

      /* If the expression refers to a signal array, then there must
         also be a word select expression, and if the signal is not an
         array, there must NOT be a word expression. */
      if (dimensions == 0 && word != 0) {
	    fprintf(out, "%*sERROR: Unexpected word expression\n", ind+2, "");
	    stub_errors += 1;
      }
      if (dimensions >= 1 && word == 0) {
	    fprintf(out, "%*sERROR: Missing word expression\n", ind+2, "");
	    stub_errors += 1;
      }

      if (word != 0) {
	    fprintf(out, "%*sAddress-0 word address:\n", ind+2, "");
	    show_expression(word, ind+2);
      }
}

static void show_ternary_expression(ivl_expr_t net, unsigned ind)
{
      unsigned width = ivl_expr_width(net);
      const char*sign = ivl_expr_signed(net)? "signed" : "unsigned";
      const char*vt = vt_type_string(net);

      fprintf(out, "%*s<ternary  width=%u, %s type=%s>\n", ind, "",
	      width, sign, vt);
      show_expression(ivl_expr_oper1(net), ind+4);
      show_expression(ivl_expr_oper2(net), ind+4);
      show_expression(ivl_expr_oper3(net), ind+4);

      if (ivl_expr_width(ivl_expr_oper2(net)) != width) {
	    fprintf(out, "ERROR: Width of TRUE expressions is %u, not %u\n",
		    ivl_expr_width(ivl_expr_oper2(net)), width);
	    stub_errors += 1;
      }

      if (ivl_expr_width(ivl_expr_oper3(net)) != width) {
	    fprintf(out, "ERROR: Width of FALSE expressions is %u, not %u\n",
		    ivl_expr_width(ivl_expr_oper3(net)), width);
	    stub_errors += 1;
      }
}

void show_expression(ivl_expr_t net, unsigned ind)
{
      unsigned idx;
      const ivl_expr_type_t code = ivl_expr_type(net);
      ivl_parameter_t par = ivl_expr_parameter(net);
      unsigned width = ivl_expr_width(net);
      const char*sign = ivl_expr_signed(net)? "signed" : "unsigned";
      const char*vt = vt_type_string(net);

      switch (code) {

	  case IVL_EX_ARRAY:
	    show_array_expression(net, ind);
	    break;

	  case IVL_EX_BINARY:
	    show_binary_expression(net, ind);
	    break;

	  case IVL_EX_CONCAT:
	    fprintf(out, "%*s<concat repeat=%u, width=%u, %s, type=%s>\n",
		    ind, "", ivl_expr_repeat(net), width, sign, vt);
	    for (idx = 0 ;  idx < ivl_expr_parms(net) ;  idx += 1)
		  show_expression(ivl_expr_parm(net, idx), ind+3);

	    break;

	  case IVL_EX_MEMORY:
	    show_memory_expression(net, ind);
	    break;

	  case IVL_EX_NUMBER: {
		const char*bits = ivl_expr_bits(net);

		fprintf(out, "%*s<number=%u'b", ind, "", width);
		for (idx = width ;  idx > 0 ;  idx -= 1)
		      fprintf(out, "%c", bits[idx-1]);

		fprintf(out, ", %s %s", sign, vt);
		if (par != 0)
		      fprintf(out, ", parameter=%s",
			      ivl_parameter_basename(par));

		fprintf(out, ">\n");
		break;
	  }

	  case IVL_EX_SELECT:
	      /* The SELECT expression can be used to express part
		 select, or if the base is null vector extension. */
	    if (ivl_expr_oper2(net)) {
		  fprintf(out, "%*s<select: width=%u, %s>\n", ind, "",
			  width, sign);
		  show_expression(ivl_expr_oper1(net), ind+3);
		  show_expression(ivl_expr_oper2(net), ind+3);
	    } else {
		  fprintf(out, "%*s<expr pad: width=%u, %s>\n", ind, "",
			  width, sign);
		  show_expression(ivl_expr_oper1(net), ind+3);
	    }
	    break;

	  case IVL_EX_STRING:
	    fprintf(out, "%*s<string=\"%s\", width=%u", ind, "",
		    ivl_expr_string(net), ivl_expr_width(net));
	    if (par != 0)
		      fprintf(out, ", parameter=%s",
			      ivl_parameter_basename(par));

	    fprintf(out, ", type=%s>\n", vt);
	    break;

	  case IVL_EX_SFUNC:
	    fprintf(out, "%*s<function=\"%s\", width=%u, %s, type=%s>\n",
		    ind, "", ivl_expr_name(net), width, sign, vt);
	    { unsigned cnt = ivl_expr_parms(net);
	      unsigned idx;
	      for (idx = 0 ;  idx < cnt ;  idx += 1)
		    show_expression(ivl_expr_parm(net, idx), ind+3);
	    }
	    break;

	  case IVL_EX_SIGNAL:
	    show_signal_expression(net, ind);
	    break;

	  case IVL_EX_TERNARY:
	    show_ternary_expression(net, ind);
	    break;

	  case IVL_EX_UNARY:
	    fprintf(out, "%*s<unary \"%c\" width=%u, %s, type=%s>\n", ind, "",
		    ivl_expr_opcode(net), width, sign, vt);
	    show_expression(ivl_expr_oper1(net), ind+4);
	    break;

	  case IVL_EX_UFUNC:
	    show_function_call(net, ind);
	    break;

	  case IVL_EX_REALNUM:
	      {
		    int idx;
		    union foo {
			  double rv;
			  unsigned char bv[sizeof(double)];
		    } tmp;
		    tmp.rv = ivl_expr_dvalue(net);
		    fprintf(out, "%*s<realnum=%f (", ind, "", tmp.rv);
		    for (idx = sizeof(double) ;  idx > 0 ;  idx -= 1)
			  fprintf(out, "%02x", tmp.bv[idx-1]);
		    fprintf(out, ")");
		    if (par != 0)
			  fprintf(out, ", parameter=%s",
				  ivl_parameter_basename(par));

		    fprintf(out, ">\n");
	      }
	      break;

	  default:
	    fprintf(out, "%*s<expr_type=%u>\n", ind, "", code);
	    break;
      }
}


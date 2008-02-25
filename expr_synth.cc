/*
 * Copyright (c) 1999-2008 Stephen Williams (steve@icarus.com)
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

# include "config.h"
# include "compiler.h"

# include  <iostream>

# include  "netlist.h"
# include  "netmisc.h"
# include  "ivl_assert.h"

NetNet* convert_to_real_const(Design*des, NetExpr*expr, NetExpr*obj)
{
      NetNet* sig;

      if (NetEConst*tmp = dynamic_cast<NetEConst*>(expr)) {
	    verireal vrl(tmp->value().as_double());
	    NetECReal rlval(vrl);
	    sig = rlval.synthesize(des);
      } else {
	    cerr << obj->get_fileline() << ": sorry: Cannot convert "
	    "bit based value (" << *expr << ") to real." << endl;
	    des->errors += 1;
	    sig = 0;
      }

      return sig;
}

  /* Note that lsig, rsig and real_args are references. */
bool process_binary_args(Design*des, NetExpr*left, NetExpr*right,
                         NetNet*&lsig, NetNet*&rsig, bool&real_args,
                         NetExpr*obj)
{
      if (left->expr_type() == IVL_VT_REAL ||
          right->expr_type() == IVL_VT_REAL) {
	    real_args = true;

	      /* Currently we will have a runtime assert if both expressions
	         are not real, though we can convert constants. */
	    if (left->expr_type() == IVL_VT_REAL) {
		  lsig = left->synthesize(des);
	    } else {
		  lsig = convert_to_real_const(des, left, obj);
	    }

	    if (right->expr_type() == IVL_VT_REAL) {
		  rsig = right->synthesize(des);
	    } else {
		  rsig = convert_to_real_const(des, right, obj);
	    }
      } else {
            real_args = false;
	    lsig = left->synthesize(des);
	    rsig = right->synthesize(des);

      }

      if (lsig == 0 || rsig == 0) return true;
      else return false;
}

NetNet* NetExpr::synthesize(Design*des)
{
      cerr << get_fileline() << ": internal error: cannot synthesize expression: "
	   << *this << endl;
      des->errors += 1;
      return 0;
}

/*
 * Make an LPM_ADD_SUB device from addition operators.
 */
NetNet* NetEBAdd::synthesize(Design*des)
{
      assert((op()=='+') || (op()=='-'));

      NetNet *lsig=0, *rsig=0;
      bool real_args=false;
      if (process_binary_args(des, left_, right_, lsig, rsig,
                              real_args, this)) {
	    return 0;
      }

      assert(expr_width() >= lsig->vector_width());
      assert(expr_width() >= rsig->vector_width());

      lsig = pad_to_width(des, lsig, expr_width());
      rsig = pad_to_width(des, rsig, expr_width());

      assert(lsig->vector_width() == rsig->vector_width());
      unsigned width=lsig->vector_width();

      perm_string path = lsig->scope()->local_symbol();
      NetNet*osig = new NetNet(lsig->scope(), path, NetNet::IMPLICIT, width);
      osig->local_flag(true);
      osig->data_type(expr_type());

      perm_string oname = osig->scope()->local_symbol();
      NetAddSub *adder = new NetAddSub(lsig->scope(), oname, width);
      connect(lsig->pin(0), adder->pin_DataA());
      connect(rsig->pin(0), adder->pin_DataB());
      connect(osig->pin(0), adder->pin_Result());
      des->add_node(adder);

      switch (op()) {
	  case '+':
	    adder->attribute(perm_string::literal("LPM_Direction"), verinum("ADD"));
	    break;
	  case '-':
	    adder->attribute(perm_string::literal("LPM_Direction"), verinum("SUB"));
	    break;
      }

      return osig;
}

/*
 * The bitwise logic operators are turned into discrete gates pretty
 * easily. Synthesize the left and right sub-expressions to get
 * signals, then just connect a single gate to each bit of the vector
 * of the expression.
 */
NetNet* NetEBBits::synthesize(Design*des)
{
      NetNet*lsig = left_->synthesize(des);
      NetNet*rsig = right_->synthesize(des);

      if (lsig == 0 || rsig == 0) return 0;

        /* You cannot do bitwise operations on real values. */
      if (lsig->data_type() == IVL_VT_REAL ||
          rsig->data_type() == IVL_VT_REAL) {
	    cerr << get_fileline() << ": error: " << human_readable_op(op_)
	         << " operator may not have REAL operands." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetScope*scope = lsig->scope();
      assert(scope);

      if (lsig->vector_width() != rsig->vector_width()) {
	    cerr << get_fileline() << ": internal error: bitwise (" << op_
		 << ") widths do not match: " << lsig->vector_width()
		 << " != " << rsig->vector_width() << endl;
	    cerr << get_fileline() << ":               : width="
		 << lsig->vector_width() << ": " << *left_ << endl;
	    cerr << get_fileline() << ":               : width="
		 << rsig->vector_width() << ": " << *right_ << endl;
	    des->errors += 1;
	    return 0;
      }

      assert(lsig->vector_width() == rsig->vector_width());
      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, lsig->vector_width());
      osig->local_flag(true);
      osig->data_type(expr_type());

      perm_string oname = scope->local_symbol();
      unsigned wid = lsig->vector_width();
      NetLogic*gate;

      switch (op()) {
	  case '&':
	    gate = new NetLogic(scope, oname, 3, NetLogic::AND, wid);
	    break;
	  case 'A':
	    gate = new NetLogic(scope, oname, 3, NetLogic::NAND, wid);
	    break;
	  case '|':
	    gate = new NetLogic(scope, oname, 3, NetLogic::OR, wid);
	    break;
	  case '^':
	    gate = new NetLogic(scope, oname, 3, NetLogic::XOR, wid);
	    break;
	  case 'O':
	    gate = new NetLogic(scope, oname, 3, NetLogic::NOR, wid);
	    break;
	  case 'X':
	    gate = new NetLogic(scope, oname, 3, NetLogic::XNOR, wid);
	    break;
	  default:
	    assert(0);
      }

      connect(osig->pin(0), gate->pin(0));
      connect(lsig->pin(0), gate->pin(1));
      connect(rsig->pin(0), gate->pin(2));

      gate->set_line(*this);
      des->add_node(gate);

      return osig;
}

NetNet* NetEBComp::synthesize(Design*des)
{

      NetNet *lsig=0, *rsig=0;
      unsigned width;
      bool real_args=false;
      if (process_binary_args(des, left_, right_, lsig, rsig,
                              real_args, this)) {
	    return 0;
      }

      if (real_args) {
	    width = 1;
      } else {
	    width = lsig->vector_width();
	    if (rsig->vector_width() > width) width = rsig->vector_width();

	    lsig = pad_to_width(des, lsig, width);
	    rsig = pad_to_width(des, rsig, width);
      }

      NetScope*scope = lsig->scope();
      assert(scope);

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, 1);
      osig->set_line(*this);
      osig->local_flag(true);
      osig->data_type(IVL_VT_LOGIC);

	/* Handle the special case of a single bit equality
	   operation. Make an XNOR gate instead of a comparator. */
      if ((width == 1) && ((op_ == 'e') || (op_ == 'E')) && !real_args) {
	    NetLogic*gate = new NetLogic(scope, scope->local_symbol(),
					 3, NetLogic::XNOR, 1);
	    gate->set_line(*this);
	    connect(gate->pin(0), osig->pin(0));
	    connect(gate->pin(1), lsig->pin(0));
	    connect(gate->pin(2), rsig->pin(0));
	    des->add_node(gate);
	    return osig;
      }

	/* Handle the special case of a single bit inequality
	   operation. This is similar to single bit equality, but uses
	   an XOR instead of an XNOR gate. */
      if ((width == 1) && ((op_ == 'n') || (op_ == 'N')) && !real_args) {
	    NetLogic*gate = new NetLogic(scope, scope->local_symbol(),
					 3, NetLogic::XOR, 1);
	    gate->set_line(*this);
	    connect(gate->pin(0), osig->pin(0));
	    connect(gate->pin(1), lsig->pin(0));
	    connect(gate->pin(2), rsig->pin(0));
	    des->add_node(gate);
	    return osig;
      }


      NetCompare*dev = new NetCompare(scope, scope->local_symbol(), width);
      dev->set_line(*this);
      des->add_node(dev);

      connect(dev->pin_DataA(), lsig->pin(0));
      connect(dev->pin_DataB(), rsig->pin(0));


      switch (op_) {
	  case '<':
	    connect(dev->pin_ALB(), osig->pin(0));
	    break;
	  case '>':
	    connect(dev->pin_AGB(), osig->pin(0));
	    break;
	  case 'E': // === ?
	    if (real_args) {
		  cerr << get_fileline() << ": error: Case equality may "
		          "not have real operands." << endl;
		  des->errors += 1;
		  return 0;
	    }
	  case 'e': // ==
	    connect(dev->pin_AEB(), osig->pin(0));
	    break;
	  case 'G': // >=
	    connect(dev->pin_AGEB(), osig->pin(0));
	    break;
	  case 'L': // <=
	    connect(dev->pin_ALEB(), osig->pin(0));
	    break;
	  case 'N': // !==
	    if (real_args) {
		  cerr << get_fileline() << ": error: Case inequality may "
		          "not have real operands." << endl;
		  des->errors += 1;
		  return 0;
	    }
	  case 'n': // !=
	    connect(dev->pin_ANEB(), osig->pin(0));
	    break;

	  default:
	    cerr << get_fileline() << ": internal error: cannot synthesize "
		  "comparison: " << *this << endl;
	    des->errors += 1;
	    return 0;
      }

      return osig;
}

NetNet* NetEBPow::synthesize(Design*des)
{
      NetNet *lsig=0, *rsig=0;
      unsigned width;
      bool real_args=false;
      if (process_binary_args(des, left_, right_, lsig, rsig,
                              real_args, this)) {
	    return 0;
      }

      if (real_args) width = 1;
      else width = expr_width();

      NetScope*scope = lsig->scope();
      assert(scope);

      NetPow*powr = new NetPow(scope, scope->local_symbol(), width,
			       lsig->vector_width(),
			       rsig->vector_width());
      des->add_node(powr);

      powr->set_signed( has_sign() );
      powr->set_line(*this);

      connect(powr->pin_DataA(), lsig->pin(0));
      connect(powr->pin_DataB(), rsig->pin(0));

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, width);
      osig->set_line(*this);
      osig->data_type(expr_type());
      osig->local_flag(true);

      connect(powr->pin_Result(), osig->pin(0));

      return osig;
}

NetNet* NetEBMult::synthesize(Design*des)
{
      NetNet *lsig=0, *rsig=0;
      unsigned width;
      bool real_args=false;
      if (process_binary_args(des, left_, right_, lsig, rsig,
                              real_args, this)) {
	    return 0;
      }

      if (real_args) width = 1;
      else width = expr_width();

      NetScope*scope = lsig->scope();
      assert(scope);

      NetMult*mult = new NetMult(scope, scope->local_symbol(),
				 width,
				 lsig->vector_width(),
				 rsig->vector_width());
      des->add_node(mult);

      mult->set_signed( has_sign() );
      mult->set_line(*this);

      connect(mult->pin_DataA(), lsig->pin(0));
      connect(mult->pin_DataB(), rsig->pin(0));

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, width);
      osig->set_line(*this);
      osig->data_type(expr_type());
      osig->local_flag(true);

      connect(mult->pin_Result(), osig->pin(0));

      return osig;
}

NetNet* NetEBDiv::synthesize(Design*des)
{
      NetNet *lsig=0, *rsig=0;
      unsigned width;
      bool real_args=false;
      if (process_binary_args(des, left_, right_, lsig, rsig,
                              real_args, this)) {
	    return 0;
      }

      if (real_args) width = 1;
      else width = expr_width();

      NetScope*scope = lsig->scope();

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, width);
      osig->set_line(*this);
      osig->data_type(lsig->data_type());
      osig->local_flag(true);

      switch (op()) {

	  case '/': {
		NetDivide*div = new NetDivide(scope, scope->local_symbol(),
					      width,
					      lsig->vector_width(),
					      rsig->vector_width());
		div->set_line(*this);
		des->add_node(div);

		connect(div->pin_DataA(), lsig->pin(0));
		connect(div->pin_DataB(), rsig->pin(0));
		connect(div->pin_Result(),osig->pin(0));
		break;
	  }

	  case '%': {
		  /* Baseline Verilog does not support the % operator with
		     real arguments, but we allow it in our extended form. */
		if (real_args && generation_flag < GN_VER2001X) {
		      cerr << get_fileline() << ": error: Modulus operator "
		              "may not have REAL operands." << endl;
		      des->errors += 1;
		      return 0;
		}
		NetModulo*div = new NetModulo(scope, scope->local_symbol(),
					      width,
					      lsig->vector_width(),
					      rsig->vector_width());
		div->set_line(*this);
		des->add_node(div);

		connect(div->pin_DataA(), lsig->pin(0));
		connect(div->pin_DataB(), rsig->pin(0));
		connect(div->pin_Result(),osig->pin(0));
		break;
	  }

	  default: {
		cerr << get_fileline() << ": internal error: "
		     << "NetEBDiv has unexpeced op() code: "
		     << op() << endl;
		des->errors += 1;

		delete osig;
		return 0;
	  }
      }

      return osig;
}

NetNet* NetEBLogic::synthesize(Design*des)
{
      NetNet*lsig = left_->synthesize(des);
      NetNet*rsig = right_->synthesize(des);

      if (lsig == 0 || rsig == 0) return 0;

        /* You cannot currently do logical operations on real values. */
      if (lsig->data_type() == IVL_VT_REAL ||
          rsig->data_type() == IVL_VT_REAL) {
	    cerr << get_fileline() << ": sorry: " << human_readable_op(op_)
	         << " is currently unsupported for real values." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetScope*scope = lsig->scope();
      assert(scope);

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, 1);
      osig->data_type(expr_type());
      osig->local_flag(true);


      if (op() == 'o') {

	      /* Logic OR can handle the reduction *and* the logical
		 comparison with a single wide OR gate. So handle this
		 magically. */

	    perm_string oname = scope->local_symbol();

	    NetLogic*olog = new NetLogic(scope, oname,
					 lsig->pin_count()+rsig->pin_count()+1,
					 NetLogic::OR, 1);

	    connect(osig->pin(0), olog->pin(0));

	    unsigned pin = 1;
	    for (unsigned idx = 0 ;  idx < lsig->pin_count() ;  idx = 1)
		  connect(olog->pin(pin+idx), lsig->pin(idx));

	    pin += lsig->pin_count();
	    for (unsigned idx = 0 ;  idx < rsig->pin_count() ;  idx = 1)
		  connect(olog->pin(pin+idx), rsig->pin(idx));

	    des->add_node(olog);

      } else {
	    assert(op() == 'a');

	      /* Create the logic AND gate. This is a single bit
		 output, with inputs for each of the operands. */
	    NetLogic*olog;
	    perm_string oname = scope->local_symbol();

	    olog = new NetLogic(scope, oname, 3, NetLogic::AND, 1);

	    connect(osig->pin(0), olog->pin(0));
	    des->add_node(olog);

	      /* XXXX Here, I need to reduce the parameters with
		 reduction or. */


	      /* By this point, the left and right parameters have been
		 reduced to single bit values. Now we just connect them to
		 the logic gate. */
	    assert(lsig->pin_count() == 1);
	    connect(lsig->pin(0), olog->pin(1));

	    assert(rsig->pin_count() == 1);
	    connect(rsig->pin(0), olog->pin(2));
      }


      return osig;
}

NetNet* NetEBShift::synthesize(Design*des)
{
      if (! dynamic_cast<NetEConst*>(right_)) {
	    NetExpr*tmp = right_->eval_tree();
	    if (tmp) {
		  delete right_;
		  right_ = tmp;
	    }
      }

      NetNet*lsig = left_->synthesize(des);

      if (lsig == 0) return 0;

        /* Cannot shift a real values. */
      if (lsig->data_type() == IVL_VT_REAL) {
	    cerr << get_fileline() << ": error: shift operator ("
	         << human_readable_op(op_)
	         << ") cannot shift a real values." << endl;
	    des->errors += 1;
	    return 0;
      }

      bool right_flag  =  op_ == 'r' || op_ == 'R';
      bool signed_flag =  op_ == 'R';

      NetScope*scope = lsig->scope();

	/* Detect the special case where the shift amount is
	   constant. Evaluate the shift amount, and simply reconnect
	   the left operand to the output, but shifted. */
      if (NetEConst*rcon = dynamic_cast<NetEConst*>(right_)) {
	    verinum shift_v = rcon->value();
	    long shift = shift_v.as_long();

	    if (op() == 'r')
		  shift = 0-shift;

	    if (shift == 0)
		  return lsig;

	    NetNet*osig = new NetNet(scope, scope->local_symbol(),
				     NetNet::IMPLICIT, expr_width());
	    osig->data_type(expr_type());
	    osig->local_flag(true);

	      // ushift is the amount of pad created by the shift.
	    unsigned long ushift = shift>=0? shift : -shift;
	    if (ushift > osig->vector_width())
		  ushift = osig->vector_width();

	      // part_width is the bits of the vector that survive the shift.
	    unsigned long part_width = osig->vector_width() - ushift;

	    verinum znum (verinum::V0, ushift, true);
	    NetConst*zcon = new NetConst(scope, scope->local_symbol(),
					 znum);
	    des->add_node(zcon);

	      /* Detect the special case that the shift is the size of
		 the whole expression. Simply connect the pad to the
		 osig and escape. */
	    if (ushift >= osig->vector_width()) {
		  connect(zcon->pin(0), osig->pin(0));
		  return osig;
	    }

	    NetNet*zsig = new NetNet(scope, scope->local_symbol(),
				     NetNet::WIRE, znum.len());
	    zsig->data_type(osig->data_type());
	    zsig->local_flag(true);
	    zsig->set_line(*this);
	    connect(zcon->pin(0), zsig->pin(0));

	      /* Create a part select to reduce the width of the lsig
	         to the amount left by the shift. */
	    NetPartSelect*psel = new NetPartSelect(lsig, shift<0? ushift : 0,
						   part_width,
						   NetPartSelect::VP);
	    des->add_node(psel);

	    NetNet*psig = new NetNet(scope, scope->local_symbol(),
				     NetNet::IMPLICIT, part_width);
	    psig->data_type(expr_type());
	    psig->local_flag(true);
	    psig->set_line(*this);
	    connect(psig->pin(0), psel->pin(0));

	    NetConcat*ccat = new NetConcat(scope, scope->local_symbol(),
					   osig->vector_width(), 2);
	    ccat->set_line(*this);
	    des->add_node(ccat);

	    connect(ccat->pin(0), osig->pin(0));
	    if (shift > 0) {
		    // Left shift.
		  connect(ccat->pin(1), zsig->pin(0));
		  connect(ccat->pin(2), psig->pin(0));
	    } else {
		    // Right shift
		  connect(ccat->pin(1), psig->pin(0));
		  connect(ccat->pin(2), zsig->pin(0));
	    }

	    return osig;
      }

      NetNet*rsig = right_->synthesize(des);

      if (rsig == 0) return 0;

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, expr_width());
      osig->data_type(expr_type());
      osig->local_flag(true);

      NetCLShift*dev = new NetCLShift(scope, scope->local_symbol(),
				      osig->vector_width(),
				      rsig->vector_width(),
				      right_flag, signed_flag);
      dev->set_line(*this);
      des->add_node(dev);

      connect(dev->pin_Result(), osig->pin(0));

      assert(lsig->vector_width() == dev->width());
      connect(dev->pin_Data(), lsig->pin(0));

      connect(dev->pin_Distance(), rsig->pin(0));

      return osig;
}

NetNet* NetEConcat::synthesize(Design*des)
{
	/* First, synthesize the operands. */
      NetNet**tmp = new NetNet*[parms_.count()];
      bool flag = true;
      for (unsigned idx = 0 ;  idx < parms_.count() ;  idx += 1) {
	    tmp[idx] = parms_[idx]->synthesize(des);
	    if (tmp[idx] == 0)
		  flag = false;
      }

      if (flag == false)
	    return 0;

      assert(tmp[0]);
      NetScope*scope = tmp[0]->scope();
      assert(scope);

	/* Make a NetNet object to carry the output vector. */
      perm_string path = scope->local_symbol();
      NetNet*osig = new NetNet(scope, path, NetNet::IMPLICIT, expr_width());
      osig->local_flag(true);
      osig->data_type(tmp[0]->data_type());

      NetConcat*concat = new NetConcat(scope, scope->local_symbol(),
				       osig->vector_width(),
				       parms_.count() * repeat());
      concat->set_line(*this);
      des->add_node(concat);
      connect(concat->pin(0), osig->pin(0));

      unsigned cur_pin = 1;
      for (unsigned rpt = 0; rpt < repeat(); rpt += 1) {
	    for (unsigned idx = 0 ;  idx < parms_.count() ;  idx += 1) {
		  connect(concat->pin(cur_pin), tmp[parms_.count()-idx-1]->pin(0));
		  cur_pin += 1;
	    }
      }

      delete[]tmp;
      return osig;
}

NetNet* NetEConst::synthesize(Design*des)
{
      NetScope*scope = des->find_root_scope();
      assert(scope);

      perm_string path = scope->local_symbol();
      unsigned width=expr_width();

      NetNet*osig = new NetNet(scope, path, NetNet::IMPLICIT, width-1,0);
      osig->local_flag(true);
      osig->data_type(IVL_VT_LOGIC);
      osig->set_signed(has_sign());
      NetConst*con = new NetConst(scope, scope->local_symbol(), value());
      connect(osig->pin(0), con->pin(0));

      des->add_node(con);
      return osig;
}

/*
* Create a NetLiteral object to represent real valued constants.
*/
NetNet* NetECReal::synthesize(Design*des)
{
      NetScope*scope = des->find_root_scope();
      assert(scope);

      perm_string path = scope->local_symbol();

      NetNet*osig = new NetNet(scope, path, NetNet::WIRE, 1);
      osig->local_flag(true);
      osig->data_type(IVL_VT_REAL);
      osig->set_signed(has_sign());
      osig->set_line(*this);

      NetLiteral*con = new NetLiteral(scope, scope->local_symbol(), value_);
      des->add_node(con);
      con->set_line(*this);

      connect(osig->pin(0), con->pin(0));
      return osig;
}

/*
 * The bitwise unary logic operator (there is only one) is turned
 * into discrete gates just as easily as the binary ones above.
 */
NetNet* NetEUBits::synthesize(Design*des)
{
      NetNet*isig = expr_->synthesize(des);

      if (isig == 0) return 0;

      if (isig->data_type() == IVL_VT_REAL) {
	    cerr << get_fileline() << ": error: bit-wise negation ("
	         << human_readable_op(op_)
	         << ") may not have a REAL operand." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetScope*scope = isig->scope();
      assert(scope);

      unsigned width = isig->vector_width();
      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, width);
      osig->data_type(expr_type());
      osig->local_flag(true);

      perm_string oname = scope->local_symbol();
      NetLogic*gate;

      switch (op()) {
	  case '~':
	    gate = new NetLogic(scope, oname, 2, NetLogic::NOT, width);
	    break;
	  default:
	    assert(0);
      }

      connect(osig->pin(0), gate->pin(0));
      connect(isig->pin(0), gate->pin(1));

      des->add_node(gate);

      return osig;
}

NetNet* NetEUReduce::synthesize(Design*des)
{
      NetNet*isig = expr_->synthesize(des);

      if (isig == 0) return 0;

      if (isig->data_type() == IVL_VT_REAL) {
	    cerr << get_fileline() << ": error: reduction operator ("
	         << human_readable_op(op_)
	         << ") may not have a REAL operand." << endl;
	    des->errors += 1;
	    return 0;
      }

      NetScope*scope = isig->scope();
      assert(scope);

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, 1);
      osig->data_type(expr_type());
      osig->local_flag(true);

      NetUReduce::TYPE rtype = NetUReduce::NONE;

      switch (op()) {
	  case 'N':
	  case '!':
	    rtype = NetUReduce::NOR;
	    break;
	  case '&':
	    rtype = NetUReduce::AND;
	    break;
	  case '|':
	    rtype = NetUReduce::OR;
	    break;
	  case '^':
	    rtype = NetUReduce::XOR;
	    break;
	  case 'A':
	    rtype = NetUReduce::XNOR;
	    break;
	  case 'X':
	    rtype = NetUReduce::XNOR;
	    break;
	  default:
	    cerr << get_fileline() << ": internal error: "
		 << "Unable to synthesize " << *this << "." << endl;
	    return 0;
      }

      NetUReduce*gate = new NetUReduce(scope, scope->local_symbol(),
				       rtype, isig->vector_width());

      des->add_node(gate);
      connect(gate->pin(0), osig->pin(0));
      for (unsigned idx = 0 ;  idx < isig->pin_count() ;  idx += 1)
	    connect(gate->pin(1+idx), isig->pin(idx));

      return osig;
}

NetNet* NetESelect::synthesize(Design *des)
{

      NetNet*sub = expr_->synthesize(des);

      if (sub == 0) return 0;

      NetScope*scope = sub->scope();

      NetNet*off = 0;

	// This handles the case that the NetESelect exists to do an
	// actual part/bit select. Generate a NetPartSelect object to
	// do the work, and replace "sub" with the selected output.
      if (base_ != 0) {
	    off = base_->synthesize(des);

	    NetPartSelect*sel = new NetPartSelect(sub, off, expr_width());
	    sel->set_line(*this);
	    des->add_node(sel);

	    NetNet*tmp = new NetNet(scope, scope->local_symbol(),
				    NetNet::IMPLICIT, expr_width());
	    tmp->data_type(sub->data_type());
	    tmp->local_flag(true);
	    tmp->set_line(*this);
	    sub = tmp;
	    connect(sub->pin(0), sel->pin(0));
      }


	// Now look for the case that the NetESelect actually exists
	// to change the width of the expression. (i.e. to do
	// padding.) If this was for an actual part select that at
	// this point the output vector_width is exactly right, and we
	// are done.
      if (sub->vector_width() == expr_width())
	    return sub;

	// The vector_width is not exactly right, so the source is
	// probably asking for padding. Create nodes to do sign
	// extension or 0 extension, depending on the has_sign() mode
	// of the expression.

      NetNet*net = new NetNet(scope, scope->local_symbol(),
			      NetNet::IMPLICIT, expr_width());
      net->data_type(expr_type());
      net->local_flag(true);
      net->set_line(*this);
      if (has_sign()) {
	    NetSignExtend*pad = new NetSignExtend(scope,
						  scope->local_symbol(),
						  expr_width());
	    pad->set_line(*this);
	    des->add_node(pad);

	    connect(pad->pin(1), sub->pin(0));
	    connect(pad->pin(0), net->pin(0));

      } else {

	    NetConcat*cat = new NetConcat(scope, scope->local_symbol(),
					  expr_width(), 2);
	    cat->set_line(*this);
	    des->add_node(cat);

	    assert(expr_width() > sub->vector_width());
	    unsigned pad_width = expr_width() - sub->vector_width();
	    verinum pad((uint64_t)0, pad_width);
	    NetConst*con = new NetConst(scope, scope->local_symbol(),
					pad);
	    con->set_line(*this);
	    des->add_node(con);

	    NetNet*tmp = new NetNet(scope, scope->local_symbol(),
				    NetNet::IMPLICIT, pad_width);
	    tmp->data_type(expr_type());
	    tmp->local_flag(true);
	    tmp->set_line(*this);
	    connect(tmp->pin(0), con->pin(0));

	    connect(cat->pin(0), net->pin(0));
	    connect(cat->pin(1), sub->pin(0));
	    connect(cat->pin(2), con->pin(0));
      }

      return net;
}

/*
 * Synthesize a ?: operator as a NetMux device. Connect the condition
 * expression to the select input, then connect the true and false
 * expressions to the B and A inputs. This way, when the select input
 * is one, the B input, which is the true expression, is selected.
 */
NetNet* NetETernary::synthesize(Design *des)
{
      NetNet*csig = cond_->synthesize(des),
            *tsig = true_val_->synthesize(des),
            *fsig = false_val_->synthesize(des);

      if (csig == 0 || tsig == 0 || fsig == 0) return 0;

      if (tsig->data_type() != fsig->data_type()) {
	    cerr << get_fileline() << ": error: True and False clauses of "
	            "ternary expression have different types." << endl;
	    cerr << get_fileline() << ":      : True  clause is: "
	         << tsig->data_type() << endl;
	    cerr << get_fileline() << ":      : False clause is: "
	         << fsig->data_type() << endl;
	    des->errors += 1;
	    return 0;
      } else if (tsig->data_type() == IVL_VT_NO_TYPE) {
	    cerr << get_fileline() << ": internal error: True and False "
	            "clauses of ternary both have NO TYPE." << endl;
	    des->errors += 1;
	    return 0;
      }

      perm_string path = csig->scope()->local_symbol();

      assert(csig->vector_width() == 1);

      unsigned width=expr_width();
      NetNet*osig = new NetNet(csig->scope(), path, NetNet::IMPLICIT, width);
      osig->data_type(expr_type());
      osig->local_flag(true);

	/* Make sure both value operands are the right width. */
      tsig = crop_to_width(des, pad_to_width(des, tsig, width), width);
      fsig = crop_to_width(des, pad_to_width(des, fsig, width), width);

      assert(width == tsig->vector_width());
      assert(width == fsig->vector_width());

      perm_string oname = csig->scope()->local_symbol();
      NetMux *mux = new NetMux(csig->scope(), oname, width,
			       2, csig->vector_width());
      connect(tsig->pin(0), mux->pin_Data(1));
      connect(fsig->pin(0), mux->pin_Data(0));
      connect(osig->pin(0), mux->pin_Result());
      connect(csig->pin(0), mux->pin_Sel());
      des->add_node(mux);

      return osig;
}

/*
 * When synthesizing a signal expression, it is usually fine to simply
 * return the NetNet that it refers to. If this is an array word though,
 * a bit more work needs to be done. Return a temporary that represents
 * the selected word.
 */
NetNet* NetESignal::synthesize(Design*des)
{
      if (word_ == 0)
	    return net_;

      NetScope*scope = net_->scope();

      NetNet*tmp = new NetNet(scope, scope->local_symbol(),
			      NetNet::IMPLICIT, net_->vector_width());
      tmp->set_line(*this);
      tmp->local_flag(true);
      tmp->data_type(net_->data_type());

      if (NetEConst*index_co = dynamic_cast<NetEConst*> (word_)) {
	    long index = index_co->value().as_long();

	    assert(net_->array_index_is_valid(index));
	    index = net_->array_index_to_address(index);

	    connect(tmp->pin(0), net_->pin(index));
      } else {
	    unsigned selwid = word_->expr_width();

	    NetArrayDq*mux = new NetArrayDq(scope, scope->local_symbol(),
					    net_, selwid);
	    mux->set_line(*this);
	    des->add_node(mux);

	    NetNet*index_net = word_->synthesize(des);
	    connect(mux->pin_Address(), index_net->pin(0));

	    connect(tmp->pin(0), mux->pin_Result());
      }
      return tmp;
}

NetNet* NetESFunc::synthesize(Design*des)
{
      cerr << get_fileline() << ": sorry: cannot synthesize system function: "
	   << *this << " in this context" << endl;
      des->errors += 1;
      return 0;
}

NetNet* NetEUFunc::synthesize(Design*des)
{
      svector<NetNet*> eparms (parms_.count());

        /* Synthesize the arguments. */
      bool errors = false;
      for (unsigned idx = 0; idx < eparms.count(); idx += 1) {
	    NetNet*tmp = parms_[idx]->synthesize(des);
	    if (tmp == 0) {
		  cerr << get_fileline() << ": error: Unable to synthesize "
		          "port " << idx << " of call to "
		       << func_->basename() << "." << endl;
		  errors = true;
		  des->errors += 1;
		  continue;
	    }
	    eparms[idx] = tmp;
      }
      if (errors) return 0;

      NetUserFunc*net = new NetUserFunc(scope_, scope_->local_symbol(), func_);
      net->set_line(*this);
      des->add_node(net);

        /* Create an output signal and connect it to the function. */
      NetNet*osig = new NetNet(scope_, scope_->local_symbol(), NetNet::WIRE,
                               result_sig_->vector_width());
      osig->local_flag(true);
      osig->data_type(result_sig_->expr_type());
      connect(net->pin(0), osig->pin(0));

        /* Connect the pins to the arguments. */
      NetFuncDef*def = func_->func_def();
      for (unsigned idx = 0; idx < eparms.count(); idx += 1) {
	    NetNet*tmp = pad_to_width(des, eparms[idx], 
	                              def->port(idx)->vector_width());
	    connect(net->pin(idx+1), tmp->pin(0));
      }

      return osig;
}

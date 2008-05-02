#norootforbuild
#
%define rev_date 20080429
#
#
Summary: Icarus Verilog
Name: verilog
Version: 0.9.0.%{rev_date}
Release: 0
License: GPL
Group: Productivity/Scientific/Electronics
Source: verilog-%{rev_date}.tar.gz
URL: http://www.icarus.com/eda/verilog/index.html
Packager: Stephen Williams <steve@icarus.com>

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

BuildRequires: gcc-c++, zlib-devel, bison, flex, gperf, readline-devel

# This provides tag allows me to use a more specific name for things
# that actually depend on me, Icarus Verilog.
Provides: iverilog

%description
Icarus Verilog is a Verilog compiler that generates a variety of
engineering formats, including simulation. It strives to be true
to the IEEE-1364 standard.

%prep
%setup -n verilog-%{rev_date}

%build
%{configure}
make CXXFLAGS=-O

%install
%{makeinstall}

%clean
rm -rf $RPM_BUILD_ROOT

%files

%attr(-,root,root) %doc COPYING README.txt BUGS.txt QUICK_START.txt ieee1364-notes.txt mingw.txt swift.txt netlist.txt t-dll.txt vpi.txt xnf.txt tgt-fpga/fpga.txt cadpli/cadpli.txt xilinx-hint.txt
%attr(-,root,root) %doc examples/*

%attr(-,root,root) %{_mandir}/man1/iverilog.1.gz
#%attr(-,root,root) /usr/man/man1/iverilog-fpga.1.gz
%attr(-,root,root) %{_mandir}/man1/iverilog-vpi.1.gz
%attr(-,root,root) %{_mandir}/man1/vvp.1.gz

%attr(-,root,root) %{_bindir}/iverilog
%attr(-,root,root) %{_bindir}/iverilog-vpi
%attr(-,root,root) %{_bindir}/vvp
%attr(-,root,root) %{_libdir}/ivl/ivl
%attr(-,root,root) %{_libdir}/ivl/ivlpp
%attr(-,root,root) %{_libdir}/ivl/null.tgt
%attr(-,root,root) %{_libdir}/ivl/null.conf
%attr(-,root,root) %{_libdir}/ivl/null-s.conf
%attr(-,root,root) %{_libdir}/ivl/stub.tgt
%attr(-,root,root) %{_libdir}/ivl/stub.conf
%attr(-,root,root) %{_libdir}/ivl/stub-s.conf
%attr(-,root,root) %{_libdir}/ivl/vvp.tgt
%attr(-,root,root) %{_libdir}/ivl/vvp.conf
%attr(-,root,root) %{_libdir}/ivl/vvp-s.conf
#%attr(-,root,root) %{_libdir}/ivl/fpga.tgt
#%attr(-,root,root) %{_libdir}/ivl/fpga.conf
#%attr(-,root,root) %{_libdir}/ivl/fpga-s.conf
#%attr(-,root,root) %{_libdir}/ivl/xnf.conf
#%attr(-,root,root) %{_libdir}/ivl/xnf-s.conf
%attr(-,root,root) %{_libdir}/ivl/system.sft
%attr(-,root,root) %{_libdir}/ivl/system.vpi
%attr(-,root,root) %{_libdir}/ivl/cadpli.vpl
%attr(-,root,root) %{_libdir}/libvpi.a
%attr(-,root,root) %{_libdir}/libveriuser.a
%attr(-,root,root) /usr/include/ivl_target.h
%attr(-,root,root) /usr/include/vpi_user.h
%attr(-,root,root) /usr/include/acc_user.h
%attr(-,root,root) /usr/include/veriuser.h
%attr(-,root,root) /usr/include/_pli_types.h

%changelog -n verilog
* Fri Jan 25 2008 - steve@icarus.com
- Removed vvp32 support for x86_64 build.

* Sun Feb 28 2007 - steve@icarus.com
- Added formatting suitable for openSUSE packaging.

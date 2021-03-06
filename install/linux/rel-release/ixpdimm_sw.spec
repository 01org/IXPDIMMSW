%define package_name ixpdimm_sw
%define product_name ixpdimm
%define api_name lib%{product_name}
%define api_dname %{product_name}-devel
%define data_name %{product_name}-data
%define cim_lib_name lib%{product_name}-cim
%define monitor_name %{product_name}-monitor
%define cli_name %{product_name}-cli
%define cli_lib_name lib%{product_name}-cli

%define build_version 99.99.99.9999
%define invm_framework_build_version 99.99.99.9999
%define _unpackaged_files_terminate_build 0

Name: %{package_name}
Version: %{build_version}
Release: 1%{?dist}
Summary: API for development of IXPDIMM management utilities
License: BSD
Group: Applications/System
URL: https://01.org/ixpdimm-sw
Source: https://github.com/01org/ixpdimm_sw/releases/download/v%{version}/%{name}-%{version}.tar.gz
ExclusiveArch: x86_64

BuildRequires: pkgconfig(libkmod)
BuildRequires: pkgconfig(sqlite3)
BuildRequires: pkgconfig(libndctl)
BuildRequires: pkgconfig(openssl)
BuildRequires: pkgconfig(libinvm-cli)
BuildRequires: pkgconfig(libinvm-cim)
BuildRequires: pkgconfig(libinvm-i18n)
BuildRequires: numactl-devel
BuildRequires: sblim-cmpi-devel
BuildRequires: python2
BuildRequires: cmake
BuildRequires: gettext
BuildRequires: gcc
BuildRequires: gcc-c++

%description
An application program interface (API) which provides programmatic access to
the IXPSIMM SW functionality.

%prep
%setup -q -n %{name}-%{version}

%package -n %{api_name}
Summary:        API for development of %{product_name} management utilities
Group:          System/Libraries
Requires:       %{data_name}
Requires:       ndctl-libs >= 58.2
Requires:       libinvm-i18n >= 01.01
Obsoletes:      ixpdimm_sw
Obsoletes:      libixpdimm-core

%description -n %{api_name}
An application program interface (API) which provides programmatic access to
the IXPSIMM SW functionality.

%package -n %{api_dname}
Summary:        Development files for %{name}
Group:          Development/Libraries
Requires:       %{api_name}%{?_isa} = %{version}-%{release}
Obsoletes:      ixpdimm_sw-devel

%description -n %{api_dname}
The %{api_dname} package contains header files for
developing applications that use IXPDIMM SW.

%package -n %{data_name}
Summary:        Data files for %{package_name}
Group:          System/Libraries
Conflicts:      ixpdimm_sw

%description -n %{data_name}
Data files for %{package_name}

%package -n %{cim_lib_name}
Summary:        CIM provider library for IXPDIMM SW
Group:          Development/Libraries
Requires:       %{api_name}%{?_isa} = %{version}-%{release}
Requires:       libinvm-cim >= 01.01
Requires:       pywbem
Requires(pre):  pywbem
Requires(post): pywbem

%description -n %{cim_lib_name}
A Common Information Model (CIM) provider library to expose the IXPDIMM SW
functionality as standard CIM objects to plug-in to common information
model object managers (CIMOMs).

%package -n %{monitor_name}
Summary:        Daemon for monitoring the status of IXPDIMM
Group:          System Environment/Daemons
Requires:       %{cim_lib_name}%{?_isa} = %{version}-%{release}
%{?systemd_requires}
BuildRequires:  systemd

%description -n %{monitor_name}
A monitor daemon for monitoring the health and status of IXPDIMMs.

%package -n %{cli_name}
Summary:        CLI for management of IXPDIMM
Group:          Development/Tools
Requires:       %{cli_lib_name}%{?_isa} = %{version}-%{release}
Requires:       libinvm-cli >= 01.01
Requires:       libinvm-i18n >= 01.01

%description -n %{cli_name}
A Command Line Interface (CLI) application for configuring and
managing IXPDIMMs from the command line.

%package -n %{cli_lib_name}
Summary:        CLI for managment of %{product_name}
Group:          System/Management
Requires:       %{cim_lib_name}%{?_isa} = %{version}-%{release}
Requires:       libinvm-cli >= 01.01
Requires:       libinvm-cim >= 01.01
Requires:       libinvm-i18n >= 01.01

%description -n %{cli_lib_name}
A library for IXPDIMM CLI applications

%build
%cmake -DBUILDNUM=%{version} -DCMAKE_INSTALL_PREFIX=/usr -DRELEASE=ON \
    -DRPM_BUILD=ON -DLINUX_PRODUCT_NAME=%{name} \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DCMAKE_INSTALL_INCLUDEDIR=%{_includedir} \
    -DCMAKE_INSTALL_BINDIR=%{_bindir} \
    -DCMAKE_INSTALL_DATAROOTDIR=%{_datadir} \
    -DCMAKE_INSTALL_MANDIR=%{_mandir} \
    -DCMAKE_INSTALL_FULL_LOCALSTATEDIR=%{_localstatedir} \
    -DINSTALL_UNITDIR=%{_unitdir} \
    -DCFLAGS_EXTERNAL="%{?optflags}" \
    -DEXTERNAL=ON
make -f Makefile %{?_smp_mflags}

%install
%{!?_cmake_version: cd build}
make -f Makefile install DESTDIR=%{buildroot}

%post -n %{monitor_name}
%systemd_post ixpdimm-monitor.service

%post -n %{api_name} -p /sbin/ldconfig
%post -n %{cli_lib_name} -p /sbin/ldconfig
%post -n %{cim_lib_name} -p /sbin/ldconfig

%postun -n %{api_name} -p /sbin/ldconfig
%postun -n %{cli_lib_name} -p /sbin/ldconfig
%postun -n %{cim_lib_name} -p /sbin/ldconfig

%preun -n %{monitor_name}
%systemd_preun stop ixpdimm-monitor.service

%postun -n  %{monitor_name}
%systemd_postun_with_restart ixpdimm-monitor.service

%files -n %{api_name}
%defattr(-,root,root)
%doc README.md
%{_libdir}/libixpdimm.so.*
%{_libdir}/libixpdimm-core.so.*
%{_libdir}/libixpdimm-common.so.*
%license LICENSE

%files -n %{api_dname}
%defattr(-,root,root)
%doc README.md
%{_libdir}/libixpdimm.so
%{_libdir}/libixpdimm-core.so
%{_libdir}/libixpdimm-common.so
%{_libdir}/libixpdimm-cli.so
%{_libdir}/libixpdimm-cim.so
%attr(644,root,root) %{_includedir}/nvm_types.h
%attr(644,root,root) %{_includedir}/nvm_management.h
%attr(644,root,root) %{_includedir}/export_api.h
%license LICENSE

%files -n %{data_name}
%defattr(644,root,root)
%dir %{_sharedstatedir}/%{name}
%{_sharedstatedir}/%{name}/*.pem
%config %{_sharedstatedir}/%{name}/*.dat*

%files -n %{cim_lib_name}
%defattr(-,root,root)
%doc README.md
%{_libdir}/libixpdimm-cim.so.*
%license LICENSE

%files -n %{monitor_name}
%defattr(-,root,root)
%{_bindir}/ixpdimm-monitor
%{_unitdir}/ixpdimm-monitor.service
%attr(644,root,root) %{_mandir}/man8/ixpdimm-monitor*
%license LICENSE

%files -n %{cli_name}
%defattr(-,root,root)
%{_bindir}/ixpdimm-cli
%attr(644,root,root) %{_mandir}/man8/ixpdimm-cli*
%license LICENSE

%files -n %{cli_lib_name}
%defattr(-,root,root)
%{_libdir}/libixpdimm-cli.so.*

%changelog

{
	pkgs ? import (fetchTarball "https://github.com/NixOS/nixpkgs/archive/54644f409ab471e87014bb305eac8c50190bcf48.tar.gz") {}
}:

pkgs.mkShell {
	name="dev-environment";
	buildInputs = [
		pkgs.gnumake42
		pkgs.ccache
		pkgs.expect
		pkgs.coreutils
		pkgs.zlib
		pkgs.openssl
		pkgs.SDL
		pkgs.glibc
		pkgs.rsyslog
		pkgs.libxml2
	];
	shellHook = ''
		echo "Start developing..."
		'';
}

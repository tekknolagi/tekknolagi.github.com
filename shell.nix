{ pkgs ? import (builtins.fetchTarball "https://github.com/NixOS/nixpkgs/archive/5d43a8764fa6b933eb01b8bb0f4bd253226bf261.tar.gz") {} }:
  pkgs.mkShell {
    nativeBuildInputs = with pkgs; [
       jekyll
       rubyPackages.jekyll-feed
       ];
}

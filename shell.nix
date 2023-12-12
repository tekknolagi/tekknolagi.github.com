{ pkgs ? import <nixpkgs> {} }:
  pkgs.mkShell {
    nativeBuildInputs = with pkgs; [
       jekyll
       rubyPackages.jekyll-feed
       # rubyPackages.webrick
       # rubyPackages.jekyll-redirect-from
       ];
}

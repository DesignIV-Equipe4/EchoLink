{
  description = "EchoLink dev shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true; # stm32cubemx is unfree
      };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          stm32cubemx # must match MxCube.Version in EchoLink.ioc (6.18.1)
          gcc-arm-embedded
          cmake
          ninja
          clang-tools
          openocd
        ];
      };
    };
}

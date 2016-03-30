#!/bin/bash
# -*- coding: utf-8, tab-width: 2 -*-


function main () {
  local PKGS=( $(<<<'
    # "lib" prefix is moved to the right for well-arranged sorting

    config-dev      lib
    event-dev       lib
    lua5.2
    lua5.2-dev      lib
    readline-dev    lib
    ssl-dev         lib

    ' sed -re 's~^\s+~~;s~\s+$~~
      s~^(\S+)\s+((lib)\-?)$~\2\1~
      /^[a-z]\S+$/!d

      # also install non-dev pkg for *-dev?
      # /\-dev$/{p;s!\-dev$!!}

    ' # | tee /dev/stderr
    ) )
  sudo apt-get install "${PKGS[@]}"
  return $?
}













main "$@"; exit $?

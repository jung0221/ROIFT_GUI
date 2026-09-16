# Sourced before roift_gui starts, with ROIFT_CXXRT_DIR pointing at the bundled
# libstdc++/libgcc_s. Puts them on the loader path only when they are newer than
# the host's: conda's Qt, VTK and ITK need a newer C++ runtime than older distros
# ship, while on a newer host the GPU driver loaded into the process may need the
# host's own. libstdc++ is backward compatible only, so the newer one is safe.
roift_glibcxx() { grep -ao 'GLIBCXX_3\.4\.[0-9]*' "$1" 2>/dev/null | sort -uV | tail -n 1; }
roift_bundled=$(roift_glibcxx "$ROIFT_CXXRT_DIR/libstdc++.so.6")
roift_host=
for roift_dir in /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib64 /lib64 /usr/lib /lib; do
  if [ -e "$roift_dir/libstdc++.so.6" ]; then
    roift_host=$(roift_glibcxx "$roift_dir/libstdc++.so.6")
    break
  fi
done
if [ -n "$roift_bundled" ] && [ "$roift_bundled" != "$roift_host" ] &&
   [ "$(printf '%s\n%s\n' "$roift_host" "$roift_bundled" | sort -V | tail -n 1)" = "$roift_bundled" ]; then
  LD_LIBRARY_PATH="$ROIFT_CXXRT_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export LD_LIBRARY_PATH
fi
unset roift_bundled roift_host roift_dir

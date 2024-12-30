file(REMOVE_RECURSE
  "libturbohash.a"
  "libturbohash.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/turbohash.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()

# Benchmarks. Off by default (SUPERSONIC_BUILD_BENCHMARKS): they are for measuring a
# change on a machine, not for CI, and a game building the engine as a subproject
# should not compile them.
#
# Each is one .cpp linked against SupersonicCore, like a suite, but not registered
# with ctest - a timing has no pass or fail, and a flaky threshold would be worse
# than none. They print numbers (and, for the physics step, a checksum) to compare
# before and after a change. Build in Release.

function(supersonic_add_benchmark name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE SupersonicCore)
    target_compile_features(${name} PRIVATE cxx_std_20)
    set_target_properties(${name} PROPERTIES FOLDER "benchmarks")
endfunction()

supersonic_add_benchmark(physics_bench)
supersonic_add_benchmark(dedupe_bench)

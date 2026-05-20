# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-src")
  file(MAKE_DIRECTORY "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-src")
endif()
file(MAKE_DIRECTORY
  "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-build"
  "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-subbuild/antlr4-populate-prefix"
  "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-subbuild/antlr4-populate-prefix/tmp"
  "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-subbuild/antlr4-populate-prefix/src/antlr4-populate-stamp"
  "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-subbuild/antlr4-populate-prefix/src"
  "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-subbuild/antlr4-populate-prefix/src/antlr4-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-subbuild/antlr4-populate-prefix/src/antlr4-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/mnt/d/VSCodeProjects/compiler/build-test/_deps/antlr4-subbuild/antlr4-populate-prefix/src/antlr4-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()

# Generates an MSVC import library (vulkan-1.lib) from the Vulkan loader DLL
# that ships with the graphics driver.
#
# The linker cannot consume a .dll directly - passing one produces
# "LNK1107: invalid or corrupt file". When no Vulkan SDK is installed there is
# no vulkan-1.lib on the system, so we synthesise one from the loader's export
# table: dumpbin lists the exports, we emit a .def, and lib.exe turns that into
# an import library.
#
# Invoked in script mode from the top-level CMakeLists:
#   cmake -DDUMPBIN=... -DLIBTOOL=... -DDLL=... -DOUTDIR=... -P this-file

foreach (_required DUMPBIN LIBTOOL DLL OUTDIR)
    if (NOT DEFINED ${_required})
        message(FATAL_ERROR "GenerateVulkanImportLib: ${_required} not set")
    endif()
endforeach()

if (NOT EXISTS "${DLL}")
    message(FATAL_ERROR "GenerateVulkanImportLib: no such file: ${DLL}")
endif()

execute_process(
    COMMAND "${DUMPBIN}" /exports "${DLL}"
    OUTPUT_VARIABLE _exportsText
    ERROR_VARIABLE  _exportsErr
    RESULT_VARIABLE _exportsResult)

if (NOT _exportsResult EQUAL 0)
    message(FATAL_ERROR "dumpbin failed on ${DLL}:\n${_exportsErr}")
endif()

# Export table rows look like:
#           1    0 00001000 vkAcquireNextImage2KHR
string(REGEX MATCHALL "[\r\n]+[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+[A-Za-z_][A-Za-z0-9_]*"
       _rows "${_exportsText}")

set(_symbols "")
foreach (_row ${_rows})
    string(REGEX MATCH "[A-Za-z_][A-Za-z0-9_]*$" _sym "${_row}")
    if (_sym)
        list(APPEND _symbols "${_sym}")
    endif()
endforeach()

list(REMOVE_DUPLICATES _symbols)
list(LENGTH _symbols _count)

if (_count LESS 50)
    message(FATAL_ERROR
        "Parsed only ${_count} exports from ${DLL}; expected the full Vulkan loader "
        "export table. Refusing to generate a truncated import library.")
endif()

set(_defBody "EXPORTS\n")
foreach (_sym ${_symbols})
    string(APPEND _defBody "    ${_sym}\n")
endforeach()

set(_defPath "${OUTDIR}/vulkan-1.def")
file(WRITE "${_defPath}" "${_defBody}")

execute_process(
    # execute_process passes each argument verbatim, so these must NOT carry
    # embedded quotes - lib.exe would treat them as part of the filename.
    COMMAND "${LIBTOOL}" /nologo /def:${_defPath} /out:${OUTDIR}/vulkan-1.lib /machine:x64
    OUTPUT_VARIABLE _libOut
    ERROR_VARIABLE  _libOut
    RESULT_VARIABLE _libResult)

if (NOT _libResult EQUAL 0)
    message(FATAL_ERROR "lib.exe failed to build the import library:\n${_libOut}")
endif()

message(STATUS "Generated vulkan-1.lib from ${_count} loader exports")

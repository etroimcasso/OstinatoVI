# Runs ostinato-damage-trace's check as a test: the cartridge FF6_VANILLA_ROM names is started on the
# engine's machine and 20 drawn scenarios go through each routine the tool traces, the cartridge's
# answer compared with the port's (tools/damage-trace/README.md, § Checking).
#
# The test passes on the tool's exit code alone. The directory the tool writes its log into is removed
# whatever the outcome; nothing the tool writes is read or kept.
#
#   cmake -DTOOL=<path to ostinato-damage-trace> -DDIRECTORY=<scratch directory>
#         -P cmake/run_damage_trace_check.cmake
#
# CMakeLists.txt registers it as the test DamageTraceTool.ChecksEveryRoutineBriefly. It fails, naming
# the variable, where FF6_VANILLA_ROM is unset or empty, and fails with the tool's exit code where the
# check does not pass. The tool is run with no shell between, so a cartridge path with spaces or
# brackets reaches it whole.

if(NOT DEFINED TOOL OR NOT DEFINED DIRECTORY)
    message(FATAL_ERROR "run_damage_trace_check.cmake needs -DTOOL=<tool> and -DDIRECTORY=<directory>")
endif()

if("$ENV{FF6_VANILLA_ROM}" STREQUAL "")
    message(FATAL_ERROR "FF6_VANILLA_ROM is not set. The damage trace's check needs a cartridge to "
                        "read; set it to the path of one.")
endif()

execute_process(
    COMMAND "${TOOL}" "$ENV{FF6_VANILLA_ROM}" "${DIRECTORY}" --check 20
    RESULT_VARIABLE checked)
file(REMOVE_RECURSE "${DIRECTORY}")

if(NOT checked STREQUAL "0")
    message(FATAL_ERROR "ostinato-damage-trace --check 20 exited with ${checked}")
endif()

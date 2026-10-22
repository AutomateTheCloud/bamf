#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Log line format, colors and file content output

load test_helper

setup() {
    setup_bamf
}

# a script on its own needs no padding: the name column is as wide as its name
PLAIN_PREFIX="[bats_test]"

@test "plain log line: identity padded with dots, then the message" {
    load_bamf
    [ "$(log "plain message")" = "${PLAIN_PREFIX} plain message" ]
}

@test "plain log line: the name column, padded with dots to BAMF_LOG_IDENTITY_WIDTH when that is wider" {
    export BAMF_LOG_IDENTITY_WIDTH=12
    load_bamf "short"
    [ "$(log "msg")" = "[short]....... msg" ]
    load_bamf "a_really_long_script_identity_name"
    _log_build_prefix
    [ "$(log "msg")" = "[a_really_long_script_identity_name] msg" ]
}

@test "message types add their prefixes" {
    load_bamf
    [ "$(log_success "s")" = "${PLAIN_PREFIX} SUCCESS: s" ]
    [ "$(log_warning "w")" = "${PLAIN_PREFIX} WARNING: w" ]
    [ "$(log_error "e")" = "${PLAIN_PREFIX} ERROR: e" ]
    [ "$(log_notice "n")" = "${PLAIN_PREFIX} n" ]
    [ "$(log_highlight "Initialize")" = "${PLAIN_PREFIX} [[ Initialize ]]" ]
    [ "$(log "x" SSTR)" = "${PLAIN_PREFIX} SCRIPT_START: x" ]
    [ "$(log "x" SFIN)" = "${PLAIN_PREFIX} SCRIPT_FINISHED: x" ]
    [ "$(log "x" SNFO)" = "${PLAIN_PREFIX} SCRIPT_INFO: x" ]
    [ "$(log "x" FILE)" = "${PLAIN_PREFIX} FILE_CONTENT: x" ]
    [ "$(log "x" SCLI)" = "${PLAIN_PREFIX} CLI: x" ]
    [ "$(log "x" succ)" = "${PLAIN_PREFIX} SUCCESS: x" ]
    [ "$(log "x" UNKNOWN)" = "${PLAIN_PREFIX} x" ]
}

@test "quiet message types print nothing" {
    load_bamf
    [ -z "$(log_quiet "q"; log "x" QUIE; log "x" SINT; log "x" SHLT)" ]
}

@test "message text is printed as-is (backslashes, percent signs, echo options)" {
    load_bamf
    [ "$(log 'C:\temp\new and \c and 100% %s %d')" = "${PLAIN_PREFIX} C:\\temp\\new and \\c and 100% %s %d" ]
    [ "$(log '-n')" = "${PLAIN_PREFIX} -n" ]
}

@test "timestamp: date and time before the identity" {
    export BAMF_ENABLE_TIMESTAMP=yes
    load_bamf
    [[ "$(log "msg")" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}\ [0-9]{2}:[0-9]{2}:[0-9]{2}\ \[bats_test\]\ msg$ ]]
}

@test "color: BAMF_ENABLE_COLOR wins over the script's ENABLE_COLOR" {
    ENABLE_COLOR=yes
    load_bamf
    [ "${ENABLE_COLOR}" = "no" ]
}

@test "color: NO_COLOR turns color off, even when the script asks for it" {
    unset BAMF_ENABLE_COLOR
    export NO_COLOR=1
    ENABLE_COLOR=yes
    load_bamf
    [ "${ENABLE_COLOR}" = "no" ]
}

@test "color: BAMF_ENABLE_COLOR=yes wins over NO_COLOR" {
    export BAMF_ENABLE_COLOR=yes NO_COLOR=1
    load_bamf
    [ "${ENABLE_COLOR}" = "yes" ]
}

@test "color: off when output is not a terminal and nothing is set" {
    unset BAMF_ENABLE_COLOR
    load_bamf
    [ "${ENABLE_COLOR}" = "no" ]
}

@test "color: on a terminal, on by default and forced off by BAMF_ENABLE_COLOR, NO_COLOR or ENABLE_COLOR" {
    command -v script >/dev/null || skip "script (util-linux) is not installed"
    unset BAMF_ENABLE_COLOR
    local CHECK="source '${BAMF_ROOT}/bamf.inc'; log x"
    # script runs the command under a pseudo-terminal, so the output is a terminal
    [[ "$(script -qec "bash -c \"${CHECK}\"" /dev/null)" == *$'\e['* ]]
    [[ "$(BAMF_ENABLE_COLOR=no script -qec "bash -c \"${CHECK}\"" /dev/null)" != *$'\e['* ]]
    [[ "$(NO_COLOR=1 script -qec "bash -c \"${CHECK}\"" /dev/null)" != *$'\e['* ]]
    [[ "$(script -qec "bash -c \"ENABLE_COLOR=no; ${CHECK}\"" /dev/null)" != *$'\e['* ]]
}

@test "color log line: exact escape sequences (parent palette)" {
    export BAMF_ENABLE_COLOR=yes
    load_bamf
    local DG=$'\e[0;90m' IDENT=$'\e[1;32m' MSG=$'\e[1;37m' RED=$'\e[0;31m'
    [ "$(log "msg")" = "${DG}[${IDENT}bats_test${MSG}${DG}] ${MSG}${MSG}${MSG}msg${MSG}" ]
    [ "$(log_error "bad")" = "${DG}[${IDENT}bats_test${MSG}${DG}] ${RED}ERROR: ${MSG}${RED}bad${MSG}" ]
}

@test "color log line: child palette (RUN_SCRIPT_AS_CHILD) and content color override" {
    export BAMF_ENABLE_COLOR=yes RUN_SCRIPT_AS_CHILD=yes
    load_bamf
    local DG=$'\e[0;90m' IDENT=$'\e[0;32m' MSG=$'\e[0;37m' GREEN=$'\e[0;32m' YELLOW=$'\e[0;33m'
    [ "$(log "done" SFIN "${LOG_COLOR_SUCCESS}")" = "${DG}[${IDENT}bats_test${MSG}${DG}] "$'\e[0;35m'"SCRIPT_FINISHED: ${MSG}${GREEN}done${MSG}" ]
    # an override given as a color definition ('\e[...') is converted as well
    [ "$(log "x" MESG "${COLOR_YELLOW}")" = "${DG}[${IDENT}bats_test${MSG}${DG}] ${MSG}${MSG}${YELLOW}x${MSG}" ]
}

@test "color_text: colors only when color is enabled, text never interpreted" {
    load_bamf
    [ "$(color_text "${COLOR_YELLOW}" 'a\tb')" = 'a\tb' ]
    ENABLE_COLOR=yes
    log_load_color_parent
    [ "$(color_text "${COLOR_YELLOW}" 'a\tb')" = $'\e[0;33m''a\tb'$'\e[1;37m' ]
}

@test "color_lookup: every documented name resolves, unknown names come back unchanged" {
    load_bamf
    local NAME
    for NAME in black blue cyan dark_gray green light_blue light_cyan light_gray light_green light_magenta light_purple light_red light_yellow magenta purple red white yellow none; do
        [[ "$(color_lookup "${NAME}")" == '\e['* ]] || { echo "no color for ${NAME}"; return 1; }
    done
    [ "$(color_lookup LIGHT_CYAN)" = "${COLOR_LIGHT_CYAN}" ]
    [ "$(color_lookup orange)" = "orange" ]
}

@test "log_status: a normal log line with the status; optional padding lines up statuses" {
    load_bamf
    [ "$(log_status pass "all good")" = "${PLAIN_PREFIX} [PASS] all good" ]
    [ "$(log_status pass "all good" 9)" = "${PLAIN_PREFIX} [PASS]... all good" ]
    [ "$(log_status warning "hmm" 9)" = "${PLAIN_PREFIX} [WARNING] hmm" ]
    [ "$(log_status warning "hmm" 2)" = "${PLAIN_PREFIX} [WARNING] hmm" ]
}

@test "timestamps: on by default when the output is not a terminal, off on a terminal" {
    cd "${BATS_TEST_TMPDIR}" || return 1
    unset BAMF_ENABLE_TIMESTAMP
    load_bamf
    [ "${ENABLE_TIMESTAMP}" = "yes" ]
    command -v script >/dev/null || skip "script (util-linux) is not installed"
    printf 'source "%s/bamf.inc"\nprintf "%%s" "${ENABLE_TIMESTAMP}"\n' "${BAMF_ROOT}" > check.sh
    # script runs the command under a pseudo-terminal, so the output is a terminal
    [[ "$(script -qec "bash check.sh" /dev/null)" == "no"* ]]
    [[ "$(BAMF_ENABLE_TIMESTAMP=yes script -qec "bash check.sh" /dev/null)" == "yes"* ]]
    [ "$(ENABLE_TIMESTAMP=no bash check.sh)" = "no" ]
}

@test "line_break: a single line of dashes" {
    load_bamf
    local LINE
    LINE="$(line_break)"
    [ -n "${LINE}" ]
    [ -z "${LINE//-/}" ]
}

@test "log_add_from_file: keeps indentation and a last line without newline" {
    load_bamf
    printf '    indented\n\ttabbed\nC:\\temp\nno trailing newline' > "${BATS_TEST_TMPDIR}/in.txt"
    run log_add_from_file "${BATS_TEST_TMPDIR}/in.txt" "sample"
    [ "${status}" -eq 0 ]
    [ "${lines[0]}" = "${PLAIN_PREFIX} FILE_CONTENT: (File::[${BATS_TEST_TMPDIR}/in.txt] / Description::sample)" ]
    [ "${lines[2]}" = "[00001]     indented" ]
    [ "${lines[3]}" = $'[00002] \ttabbed' ]
    [ "${lines[4]}" = '[00003] C:\temp' ]
    [ "${lines[5]}" = "[00004] no trailing newline" ]
    [ "${#lines[@]}" -eq 7 ]
}

@test "log_add_from_file: truncates at the line limit" {
    load_bamf
    seq 1 10 > "${BATS_TEST_TMPDIR}/in.txt"
    run log_add_from_file "${BATS_TEST_TMPDIR}/in.txt" "sample" 3
    [ "${lines[4]}" = "[00003] 3" ]
    [ "${lines[5]}" = "[00004] ----- TRUNCATED AT LINE [3] -----" ]
}

@test "log_add_from_file: missing file is reported, not fatal" {
    load_bamf
    run log_add_from_file "${BATS_TEST_TMPDIR}/missing.txt" "sample"
    [ "${status}" -eq 0 ]
    [ "${lines[1]}" = "${PLAIN_PREFIX} FILE_CONTENT: ERROR: Could not load file contents [${BATS_TEST_TMPDIR}/missing.txt]" ]
}

@test "timestamp format: the script's LOG_DISPLAY_DATE_FORMAT, set before or after sourcing; BAMF_LOG_DATE_FORMAT wins" {
    export BAMF_ENABLE_TIMESTAMP=yes
    # shellcheck disable=SC2034 # read by bamf.inc
    LOG_DISPLAY_DATE_FORMAT="%H:%M:%S"
    load_bamf
    [[ "$(log "msg")" =~ ^[0-9]{2}:[0-9]{2}:[0-9]{2}\ \[bats_test\] ]]
    # shellcheck disable=SC2034 # read by log
    LOG_DISPLAY_DATE_FORMAT="%Y"
    [[ "$(log "msg")" =~ ^[0-9]{4}\ \[bats_test\] ]]
    run env BAMF_LOG_DATE_FORMAT="%Y-%m-%dT%H:%M:%S%z" bash -c "LOG_DISPLAY_DATE_FORMAT='%H'; source '${BAMF_ROOT}/bamf.inc'; log msg"
    [[ "${output}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}[+-][0-9]{4}\ \[bash\] ]]
}

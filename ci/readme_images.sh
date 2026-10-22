#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  readme_images.sh
# purpose: Renders the example scripts' colored output as SVG images for the README
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# GitHub cannot show color in a Markdown code block, so the README shows images instead. They are
# made from the real output: each example runs under a pseudo-terminal (the script command), so BAMF
# writes its colors, and the color codes are turned into SVG. Values that change on every run (PIDs,
# durations, the host name, temporary file names, dates) are replaced with fixed ones, so the images
# only change when the output does, and CI can check they are current.

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="readme_images"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-c : Check only: exit with E_CHECKSUM_MISMATCH if an image is out of date (the default)'
    '-u : Update the images'
)

###------------------------------------------------------------------------------------------------
## Load Defaults
DIRECTORY_REPOSITORY="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../bamf.inc
source "${DIRECTORY_REPOSITORY}/bamf.inc" || exit 3
REQUIRED_EXECUTABLES+=( 'script' 'cmp' )

###------------------------------------------------------------------------------------------------
## Variables
DIRECTORY_IMAGES="${DIRECTORY_REPOSITORY}/docs/images"
MODE="check"
# Each image: file name | title (also the image's text alternative) | command, run from the repository
IMAGES=(
    "hello.svg|examples/hello.sh: a successful run|examples/hello.sh -n 'Automate the Cloud'"
    "hello_timestamps.svg|examples/hello.sh with timestamps turned on|BAMF_ENABLE_TIMESTAMP=yes examples/hello.sh -n 'Automate the Cloud'"
    "pipeline_failure.svg|examples/pipeline.sh: a step fails, and its exit code travels back|examples/pipeline.sh -f"
)
# Terminal look: background, default text, the window's title bar dots
SVG_BACKGROUND="#1e1e1e"
SVG_FOREGROUND="#cccccc"
SVG_FONT_SIZE=13
SVG_LINE_HEIGHT=18
SVG_CHARACTER_TENTHS=79    # width of one character, in tenths of a pixel
SVG_PADDING=16
SVG_TOP=40

###------------------------------------------------------------------------------------------------
## Functions

## FUNCTION: ansi_color()
## - Returns the SVG color for an ANSI color code (30-37, or 90-97 for the bright colors)
## - Arguments
##   - $1: Variable name to pass the color to
##   - $2: Color code
##   - $3: Bright (bold), yes or no
function ansi_color() {
    local __AC_CODE="${2}"
    local __AC_COLOR=""
    local -a __AC_NORMAL=( "#666666" "#cd3131" "#0dbc79" "#e5e510" "#2472c8" "#bc3fbc" "#11a8cd" "#e5e5e5" )
    local -a __AC_BRIGHT=( "#767676" "#f14c4c" "#23d18b" "#f5f543" "#3b8eea" "#d670d6" "#29b8db" "#ffffff" )
    if (( __AC_CODE >= 90 )); then
        __AC_COLOR="${__AC_BRIGHT[__AC_CODE - 90]}"
    elif [[ "${3}" == "yes" ]]; then
        __AC_COLOR="${__AC_BRIGHT[__AC_CODE - 30]}"
    else
        __AC_COLOR="${__AC_NORMAL[__AC_CODE - 30]}"
    fi
    _bamf_return "${1}" "${__AC_COLOR}"
}

## FUNCTION: column_x()
## - Prints the x position of a text column. Every colored run is placed at its own column, so the
##   image does not depend on how a viewer flows text from one run to the next.
## - Arguments
##   - $1: Column (0 is the first)
function column_x() {
    local TMP_TENTHS=$(( SVG_PADDING * 10 + ${1} * SVG_CHARACTER_TENTHS ))
    printf '%s.%s' "$(( TMP_TENTHS / 10 ))" "$(( TMP_TENTHS % 10 ))"
}

## FUNCTION: xml_escape()
## - Prints text with the characters that are special in XML escaped
## - Arguments
##   - $1: Text
function xml_escape() {
    local TMP_TEXT="${1}"
    TMP_TEXT="${TMP_TEXT//&/&amp;}"
    TMP_TEXT="${TMP_TEXT//</&lt;}"
    TMP_TEXT="${TMP_TEXT//>/&gt;}"
    printf '%s' "${TMP_TEXT}"
}

## FUNCTION: render_svg()
## - Turns a file of terminal output with ANSI color codes into an SVG image of a terminal window
## - Arguments
##   - $1: Terminal output file
##   - $2: SVG file to write
##   - $3: Title (also the text alternative)
##   - $4: The command that was run, shown on the first line
function render_svg() {
    local TMP_FILE_INPUT="${1}"
    local TMP_FILE_SVG="${2}"
    local TMP_TITLE=""
    local TMP_LINE=""
    local TMP_REST=""
    local TMP_TEXT=""
    local TMP_PARAMETERS=""
    local TMP_PARAMETER=""
    local TMP_COLOR="${SVG_FOREGROUND}"
    local TMP_BRIGHT=no
    local TMP_CODE=30
    local TMP_SPANS=""
    local TMP_WIDEST=0
    local TMP_VISIBLE=0
    local TMP_Y=0
    local TMP_WIDTH=0
    local TMP_HEIGHT=0
    local -a TMP_LINES=()
    TMP_TITLE="$(xml_escape "${3}")"

    # the command line, as a prompt
    TMP_LINES+=( "<text y=\"__Y__\" xml:space=\"preserve\"><tspan x=\"$(column_x 0)\" fill=\"#0dbc79\">\$</tspan><tspan x=\"$(column_x 2)\">$(xml_escape "${4}")</tspan></text>" )
    TMP_WIDEST=$(( ${#4} + 2 ))

    while IFS= read -r TMP_LINE || [[ -n "${TMP_LINE}" ]]; do
        TMP_LINE="${TMP_LINE%$'\r'}"
        TMP_REST="${TMP_LINE}"
        TMP_SPANS=""
        TMP_VISIBLE=0
        # text runs between color codes; each code changes the color for the text after it
        while :; do
            TMP_TEXT="${TMP_REST%%$'\e['*}"
            if [[ -n "${TMP_TEXT}" ]]; then
                TMP_SPANS+="<tspan x=\"$(column_x "${TMP_VISIBLE}")\" fill=\"${TMP_COLOR}\">$(xml_escape "${TMP_TEXT}")</tspan>"
                TMP_VISIBLE=$(( TMP_VISIBLE + ${#TMP_TEXT} ))
            fi
            [[ "${TMP_REST}" == *$'\e['* ]] || break
            TMP_REST="${TMP_REST#*$'\e['}"
            TMP_PARAMETERS="${TMP_REST%%m*}"
            TMP_REST="${TMP_REST#*m}"
            for TMP_PARAMETER in ${TMP_PARAMETERS//;/ }; do
                case "${TMP_PARAMETER}" in
                    0|00) TMP_BRIGHT=no; TMP_COLOR="${SVG_FOREGROUND}";;
                    1|01) TMP_BRIGHT=yes; ansi_color TMP_COLOR "${TMP_CODE}" yes;;
                    3[0-7]|9[0-7]) TMP_CODE="${TMP_PARAMETER}"; ansi_color TMP_COLOR "${TMP_CODE}" "${TMP_BRIGHT}";;
                esac
            done
        done
        (( TMP_VISIBLE > TMP_WIDEST )) && TMP_WIDEST=${TMP_VISIBLE}
        TMP_LINES+=( "<text y=\"__Y__\" xml:space=\"preserve\">${TMP_SPANS}</text>" )
    done < "${TMP_FILE_INPUT}"

    # two characters of slack: fonts differ slightly in width from one viewer to another
    TMP_WIDTH=$(( ((TMP_WIDEST + 2) * SVG_CHARACTER_TENTHS + 9) / 10 + 2 * SVG_PADDING ))
    TMP_HEIGHT=$(( SVG_TOP + ${#TMP_LINES[@]} * SVG_LINE_HEIGHT + SVG_PADDING ))
    {
        printf '<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s" viewBox="0 0 %s %s" role="img" aria-label="%s">\n' "${TMP_WIDTH}" "${TMP_HEIGHT}" "${TMP_WIDTH}" "${TMP_HEIGHT}" "${TMP_TITLE}"
        printf '<title>%s</title>\n' "${TMP_TITLE}"
        printf '<rect width="100%%" height="100%%" rx="8" fill="%s"/>\n' "${SVG_BACKGROUND}"
        printf '<circle cx="20" cy="18" r="6" fill="#ff5f57"/><circle cx="40" cy="18" r="6" fill="#febc2e"/><circle cx="60" cy="18" r="6" fill="#28c840"/>\n'
        printf '<g font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, %s, monospace" font-size="%s" fill="%s">\n' "'DejaVu Sans Mono'" "${SVG_FONT_SIZE}" "${SVG_FOREGROUND}"
        TMP_Y=$(( SVG_TOP + SVG_LINE_HEIGHT - 4 ))
        for TMP_LINE in "${TMP_LINES[@]}"; do
            printf '%s\n' "${TMP_LINE/__Y__/${TMP_Y}}"
            TMP_Y=$(( TMP_Y + SVG_LINE_HEIGHT ))
        done
        printf '</g>\n</svg>\n'
    } > "${TMP_FILE_SVG}"
}

## FUNCTION: run_example()
## - Runs a command under a pseudo-terminal with color on, and writes its output with the values that
##   change on every run replaced by fixed ones
## - Arguments
##   - $1: Command
##   - $2: Output file
function run_example() {
    local TMP_FILE_RAW=""
    generate_temp_file TMP_FILE_RAW "raw output"
    # script gives the command a terminal, as a person running it would have
    ( cd "${DIRECTORY_REPOSITORY}" && TERM=xterm-256color COLUMNS=80 BAMF_ENABLE_COLOR=yes BAMF_ENABLE_TIMESTAMP=no \
        BAMF_DIRECTORY_TEMPORARY=/tmp HOSTNAME=build-host command script -qec "${1}" /dev/null > "${TMP_FILE_RAW}" 2>&1 ) || true
    command sed -E \
        -e 's/PID::[0-9]+/PID::4242/g' \
        -e 's/Duration::[0-9]+m [0-9]+\.[0-9]+s/Duration::0m 00.021s/g' \
        -e "s/$(return_hostname)/build-host/g" \
        -e 's#/tmp/bamf\.[A-Za-z0-9]{10}#/tmp/bamf.Xq3vT8kLmZ#g' \
        -e 's/[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}/2026-10-22T14:16:38/g' \
        -e 's/[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}/2026-10-22 14:16:38/g' \
        "${TMP_FILE_RAW}" > "${2}"
}

###------------------------------------------------------------------------------------------------
## Main
while getopts "hVcu" OPTION; do
    case "${OPTION}" in
        c) MODE="check";;
        u) MODE="update";;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic

OUT_OF_DATE=0
command mkdir -p "${DIRECTORY_IMAGES}"
for ENTRY in "${IMAGES[@]}"; do
    IFS='|' read -r IMAGE_FILE IMAGE_TITLE IMAGE_COMMAND <<< "${ENTRY}"
    generate_temp_file FILE_OUTPUT "example output"
    generate_temp_file FILE_SVG "rendered image"
    run_example "${IMAGE_COMMAND}" "${FILE_OUTPUT}"
    render_svg "${FILE_OUTPUT}" "${FILE_SVG}" "${IMAGE_TITLE}" "${IMAGE_COMMAND}"
    if cmp -s "${FILE_SVG}" "${DIRECTORY_IMAGES}/${IMAGE_FILE}"; then
        log "- up to date:  [docs/images/${IMAGE_FILE}]"
    elif [[ "${MODE}" == "update" ]]; then
        # cat, not cp: the image gets normal permissions, not the temporary file's 0600
        command cat "${FILE_SVG}" > "${DIRECTORY_IMAGES}/${IMAGE_FILE}"
        log "- updated:     [docs/images/${IMAGE_FILE}]"
    else
        log_warning "- out of date: [docs/images/${IMAGE_FILE}]"
        OUT_OF_DATE=1
    fi
done

if (( OUT_OF_DATE == 0 )); then
    log_success "The README images are up to date"
    exit_logic 0
fi
log_error "The README images are out of date; run: ci/readme_images.sh -u"
exit_logic "${E_CHECKSUM_MISMATCH}"

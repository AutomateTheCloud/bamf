#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Reading values from files, temporary files, file attributes

# shellcheck disable=SC2034 # variables are read by the library functions under test
load test_helper

setup() {
    setup_bamf
    load_bamf
    cd "${BATS_TEST_TMPDIR}" || return 1
}

@test "key/value files: write, read back, last line wins, parse string" {
    write_key_value_to_file kv.dat name "web server"
    write_key_value_to_file kv.dat port 80
    write_key_value_to_file kv.dat port 443
    printf '[prod][port]=8443\r\n' >> kv.dat
    [ "$(load_key_value_from_file "" name kv.dat)" = "web server" ]
    load_key_value_from_file PORT port kv.dat
    [ "${PORT}" = "443" ]
    [ "$(load_key_value_from_file "" port kv.dat prod)" = "8443" ]
    [ -z "$(load_key_value_from_file "" missing kv.dat)" ]
    [ -z "$(load_key_value_from_file "" name no-such-file)" ]
}

@test "key/value files: regex characters in keys are literal" {
    printf '[a.c]=dot\n[abc]=plain\n' > kv.dat
    [ "$(load_key_value_from_file "" a.c kv.dat)" = "dot" ]
    [ "$(load_key_value_from_file "" 'a*' kv.dat)" = "" ]
}

@test "explode_key_value_to_file / echo_key_value" {
    explode_key_value_to_file kv.dat "name=web;port=443;debug"
    [ "$(cat kv.dat)" = $'[name]=web\n[port]=443\n[debug]=true' ]
    [ "$(echo_key_value " Name " " v ")" = "[name]=v" ]
}

@test "properties: case, dots and underscores, spaces, export, comments, quotes" {
    printf '%s\n' '# comment' '! comment' 'db.host = "db.internal"' 'export PORT=5432' "name='my app'" 'url=https://x/?a=1&b=2' 'json={"k": "v"}' 'PORT=6543' > app.properties
    [ "$(load_property_from_file "" DB_HOST app.properties)" = "db.internal" ]
    [ "$(load_property_from_file "" db.host app.properties)" = "db.internal" ]
    [ "$(load_property_from_file "" port app.properties)" = "6543" ]
    [ "$(load_property_from_file "" name app.properties)" = "my app" ]
    [ "$(load_property_from_file "" url app.properties)" = "https://x/?a=1&b=2" ]
    [ "$(load_property_from_file "" json app.properties)" = '{"k": "v"}' ]
    [ -z "$(load_property_from_file "" comment app.properties)" ]
}

@test "load_array_properties_from_file + verify_array_key_values" {
    printf 'db.host=db\nport=5432\n' > app.properties
    KEYS=(db.host port user)
    load_array_properties_from_file "KEYS[@]" app.properties APP
    [ "${APP_DB_HOST}" = "db" ]
    [ "${APP_PORT}" = "5432" ]
    run verify_array_key_values "KEYS[@]" APP
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
    [[ "${output}" == *"key is not set (Key::user"* ]]
    REQUIRED=(db.host port)
    verify_array_key_values "REQUIRED[@]" APP
}

@test "load_array_key_values_from_file" {
    printf '[region]=us-east-1\n[app][region]=eu-west-1\n' > kv.dat
    KEYS=(region)
    load_array_key_values_from_file "KEYS[@]" kv.dat CFG
    [ "${CFG_REGION}" = "us-east-1" ]
    load_array_key_values_from_file "KEYS[@]" kv.dat APP app
    [ "${APP_REGION}" = "eu-west-1" ]
}

@test "YAML: return_yaml_string, load_array_key_values_from_yaml_file, parse_yaml" {
    printf 'name: "web"\ndb:\n  host: db.internal\n  port: 5432\n' > app.yaml
    [ "$(return_yaml_string "" name app.yaml)" = "web" ]
    [ "$(return_yaml_string "" host app.yaml)" = "db.internal" ]
    KEYS=(name port)
    load_array_key_values_from_yaml_file "KEYS[@]" app.yaml APP
    [ "${APP_NAME}" = "web" ]
    [ "${APP_PORT}" = "5432" ]
    [ "$(parse_yaml app.yaml)" = $'name="web"\ndb_host="db.internal"\ndb_port="5432"' ]
    parse_yaml_to_file app.yaml out.txt
    [ "$(sed -n 2p out.txt)" = 'db_host="db.internal"' ]
}

@test "header strings: first match, whitespace collapsed, script version and purpose" {
    printf '#!/bin/bash\n# purpose:   Does   things\n# purpose: second\n# version: 1.2.3\n## param: value\n' > script.sh
    [ "$(return_header_string purpose script.sh)" = "Does things" ]
    [ "$(return_parameter_string param script.sh)" = "value" ]
    [ "$(return_script_version script.sh)" = "1.2.3" ]
    [ "$(return_script_purpose script.sh)" = "Does things" ]
    [ "$(return_script_version no-such-file)" = "unknown" ]
    printf '# version: 9.9.9\n' > script.sh.info
    [ "$(return_script_version script.sh)" = "9.9.9" ]
}

@test "returned values are stored literally: quotes, percent signs, backslashes, -n" {
    local VALUE
    for VALUE in "it's" '100% done %s' 'back\slash \n' '-n' '-e' '$HOME' '*'; do
        printf '# name: %s\n' "${VALUE}" > values
        RESULT=""
        return_header_string name values RESULT
        [ "${RESULT}" = "${VALUE}" ] || { echo "var mode: [${VALUE}] -> [${RESULT}]"; return 1; }
        [ "$(return_header_string name values)" = "${VALUE}" ] || { echo "stdout mode: [${VALUE}]"; return 1; }
    done
}

@test "values loaded from files are never executed" {
    printf "name=x'\$(touch executed1)'\n" > props
    printf "[name]=a'; touch executed2; '\n" > kv.dat
    KEYS=(name)
    load_array_properties_from_file "KEYS[@]" props P
    load_key_value_from_file K name kv.dat
    [ ! -e executed1 ]
    [ ! -e executed2 ]
    [ "${P_NAME}" = "x'\$(touch executed1)'" ]
    [ "${K}" = "a'; touch executed2; '" ]
}

@test "generate_temp_file / generate_temp_directory: created in the temporary directory, removed by cleanup" {
    local FILE DIR
    generate_temp_file FILE "test"
    generate_temp_directory DIR "test"
    [ -f "${FILE}" ]
    [ -d "${DIR}" ]
    [ "$(stat -c %a "${FILE}")" = "600" ]
    [[ "${FILE}" == "${BAMF_DIRECTORY_TEMPORARY}"/bamf.* ]]
    [[ "${DIR}" == "${BAMF_DIRECTORY_TEMPORARY}"/bamf.* ]]
    touch "${DIR}/inside"
    cleanup
    [ ! -e "${FILE}" ]
    [ ! -e "${DIR}" ]
}

@test "cleanup: refuses paths outside the temporary directory" {
    TEMPORARY_DIRECTORIES+=( "/var/lib/bamf_not_temporary" "/tmp/../etc" "/tmp/" )
    run cleanup
    [ -d /etc ]
    [[ "${output}" == *"not located in a known temporary directory [/var/lib/bamf_not_temporary]"* ]]
    [[ "${output}" == *"not located in a known temporary directory [/tmp/../etc]"* ]]
    [[ "${output}" == *"not located in a known temporary directory [/tmp/]"* ]]
}

@test "generate_temp_file_with_content: exact content, mode 0600" {
    local FILE=""
    generate_temp_file_with_content FILE $'s3cr3t $HOME %s \\n \'q\' ' "test"
    [ -f "${FILE}" ]
    [ "$(stat -c %a "${FILE}")" = "600" ]
    [ "$(cat "${FILE}")" = $'s3cr3t $HOME %s \\n \'q\' ' ]
}

@test "generate_temp_file: exits with E_OBJECT_FAILED_TO_CREATE when the directory is not writable" {
    [ "$(id -u)" -ne 0 ] || skip "root can write to read-only directories"
    mkdir -p readonly
    chmod 555 readonly
    run generate_temp_file RESULT "test" "${BATS_TEST_TMPDIR}/readonly"
    [ "${status}" -eq "${E_OBJECT_FAILED_TO_CREATE}" ]
}

@test "file attributes: size, checksums, timestamp, missing file" {
    printf 'abc' > f.txt
    [ "$(return_filesize_of_file_in_bytes f.txt)" = "3" ]
    [ "$(return_filesize_of_file f.txt)" = "3 bytes" ]
    [ "$(return_file_md5sum f.txt)" = "900150983cd24fb0d6963f7d28e17f72" ]
    [ "$(return_file_sha1sum f.txt)" = "a9993e364706816aba3e25717850c26c9cd0d89d" ]
    [ "$(return_file_sha256sum f.txt)" = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ]
    [[ "$(return_file_last_modify_timestamp f.txt)" =~ ^[0-9]+$ ]]
    run return_file_sha256sum missing.txt
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
    [ -z "${output}" ]
}

@test "file_update_permissions / file_update_owner: missing file" {
    printf 'x' > f.txt
    file_update_permissions f.txt 0400
    [ "$(stat -c %a f.txt)" = "400" ]
    run file_update_permissions missing.txt 0400
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
    run file_update_owner missing.txt root
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
}

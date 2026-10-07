#!/usr/bin/env sh
# acme.sh deploy hook for a Power Manifold controller: installs the
# certificate over the controller's API (POST /api/v1/tls).
#
# Copy this file to ~/.acme.sh/deploy/pwrman.sh, then:
#   PWRMAN_TOKEN=<api token> acme.sh --deploy -d pm.example.com --deploy-hook pwrman
#
# PWRMAN_TOKEN  The controller's API token. Saved for renewals.
# PWRMAN_HOST   The name or address to send it to. Default: the certificate's
#               domain. Saved.
# PWRMAN_HTTP   Set to 1 to send it over plain HTTP, for the first install
#               before HTTPS is on. Not saved.
#
# Docs: https://docs.powermanifold.io/integrations/https/

# The key and the full chain go up together in one POST to /api/v1/tls.
# The controller accepts ECDSA keys only (acme.sh's default, ec-256).
# Renewals connect over HTTPS to PWRMAN_HOST and check its certificate as
# usual: the controller still presents the previous one.

#domain keyfile certfile cafile fullchain
pwrman_deploy() {
  _cdomain="$1"
  _ckey="$2"
  _ccert="$3"
  _cca="$4"
  _cfullchain="$5"

  _debug _cdomain "$_cdomain"
  _debug _ckey "$_ckey"
  _debug _cfullchain "$_cfullchain"

  _getdeployconf PWRMAN_TOKEN
  _getdeployconf PWRMAN_HOST

  if [ -z "$PWRMAN_TOKEN" ]; then
    _err "PWRMAN_TOKEN is not set: the controller's API token"
    return 1
  fi
  _host="${PWRMAN_HOST:-$_cdomain}"
  case "$_host" in
  \*.*)
    _err "the certificate is a wildcard: set PWRMAN_HOST to the controller's name"
    return 1
    ;;
  esac
  _scheme="https"
  if [ "$PWRMAN_HTTP" = "1" ]; then
    _scheme="http"
    _info "Sending over plain HTTP this once; renewals use HTTPS"
  fi
  _url="$_scheme://$_host/api/v1/tls"

  _body="$(cat "$_ckey" "$_cfullchain")"
  export _H1="Authorization: Bearer $PWRMAN_TOKEN"
  _info "Installing the certificate on $_url"
  response="$(_post "$_body" "$_url" "" "POST" "application/x-pem-file")"
  _ret="$?"
  _debug response "$response"

  if [ "$_ret" = "0" ] && _contains "$response" '"installed":true'; then
    _info "Installed: $response"
    # saved only once they work, so a mistyped token does not replace a good one
    _savedeployconf PWRMAN_TOKEN "$PWRMAN_TOKEN"
    if [ -n "$PWRMAN_HOST" ]; then
      _savedeployconf PWRMAN_HOST "$PWRMAN_HOST"
    fi
    return 0
  fi
  _err "The controller did not install the certificate: $response"
  return 1
}

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION nvm_fdw" to load this file. \quit

CREATE FUNCTION nvm_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT NOT FENCED;

CREATE FUNCTION nvm_fdw_validator(text[], oid)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT NOT FENCED;

CREATE FOREIGN DATA WRAPPER nvm_fdw
  HANDLER nvm_fdw_handler
  VALIDATOR nvm_fdw_validator;

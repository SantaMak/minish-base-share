/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: t; -*- */

#ifndef MINISH_PARSER_H
#define MINISH_PARSER_H

#include <stddef.h>

enum redirect_type {
	REDIRECT_INPUT,
	REDIRECT_OUTPUT,
	REDIRECT_APPEND
};

struct redirect {
	enum redirect_type type;
	char *path;
};

struct command {
	char **argv;
	size_t argc;
	size_t argv_capacity;

	struct redirect *redirects;
	size_t redirect_count;
	size_t redirect_capacity;
};

struct pipeline {
	struct command *commands;
	size_t command_count;
	size_t command_capacity;
	int background;
};

enum parse_error {
	PARSE_SUCCESS = 0,
	PARSE_ERROR_EMPTY_INPUT = -1,
	PARSE_ERROR_EMPTY_COMMAND = -2,
	PARSE_ERROR_UNTERMINATED_QUOTE = -3,
	PARSE_ERROR_MISSING_REDIRECT_TARGET = -4,
	PARSE_ERROR_UNEXPECTED_TOKEN = -5,
	PARSE_ERROR_OUT_OF_MEMORY = -6
};

/**
 * @brief Parse one shell command line.
 *
 * @param[in] line Null-terminated command line.
 * @param[out] pipeline Receives the parsed pipeline.
 *
 * @return PARSE_SUCCESS on success, otherwise a parse error.
 */
int parse_line(const char *line, struct pipeline *pipeline);

/**
 * @brief Release all memory owned by a pipeline.
 *
 * @param[in,out] pipeline Pipeline to release.
 */
void pipeline_free(struct pipeline *pipeline);

/**
 * @brief Return a human-readable description of a parse error.
 *
 * @param[in] error Parse error.
 *
 * @return Constant error description.
 */
const char *parse_error_string(int);

#endif /* MINISH_PARSER_H */

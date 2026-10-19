/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: t; -*- */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "parser.h"

enum token_type {
	TOKEN_WORD,
	TOKEN_PIPE,
	TOKEN_REDIRECT_INPUT,
	TOKEN_REDIRECT_OUTPUT,
	TOKEN_REDIRECT_APPEND,
	TOKEN_BACKGROUND,
	TOKEN_END
};

struct token {
	enum token_type type;
	char *text;
};

struct lexer {
	const char *input;
	size_t position;
};

struct parser {
	struct lexer lexer;
	struct token current;
};

static void command_init(struct command *command);
static void command_free(struct command *command);
static void pipeline_init(struct pipeline *pipeline);
static int command_add_argument(struct command *command, char *argument);
static int command_add_redirect(struct command *command,
								enum redirect_type type, char *path);
static int pipeline_add_command(struct pipeline *pipeline,
								struct command *command);
static int grow_array(void **array, size_t *capacity, size_t element_size);
static int lexer_next(struct lexer *lexer, struct token *token);
static void token_free(struct token *token);
static void parser_advance(struct parser *parser);
static int parse_command(struct parser *parser,
						 struct command *command);
static int parse_pipeline(struct parser *parser,
						  struct pipeline *pipeline);

static void command_init(struct command *command)
{
	if (!command)
		return;
	
	memset(command, 0, sizeof(*command));
}

static void command_free(struct command *command)
{
	size_t i;

	if (!command)
		return;	
	
	for (i = 0; i < command->argc; i++)
		if (command->argv[i])
			free(command->argv[i]);

	for (i = 0; i < command->redirect_count; i++)
		if (command->redirects[i].path)
			free(command->redirects[i].path);

	if (command->argv)
		free(command->argv);

	if (command->redirects)
		free(command->redirects);
	command_init(command);
}

static void pipeline_init(struct pipeline *pipeline)
{
	if (!pipeline)
		return;
	
	memset(pipeline, 0, sizeof(*pipeline));
}

void pipeline_free(struct pipeline *pipeline)
{
	size_t i;

	if (!pipeline)
		return;
	
	for (i = 0; i < pipeline->command_count; i++)
		command_free(&pipeline->commands[i]);

	if (pipeline->commands)
		free(pipeline->commands);
	pipeline_init(pipeline);
}

static int grow_array(void **array, size_t *capacity, size_t element_size)
{
	size_t new_capacity;
	void *new_array;

	if (!array || !capacity)
		return -1;	
	
	if (*capacity == 0)
		new_capacity = 4;
	else
		new_capacity = *capacity * 2;

	new_array = realloc(*array, new_capacity * element_size);
	if (!new_array)
		return -1;

	*array = new_array;
	*capacity = new_capacity;

	return 0;
}

static int command_add_argument(struct command *command, char *argument)
{
	char **new_argv;
		
	if (!command || !argument)
		return -1;	

	if (command->argc + 1 >= command->argv_capacity) {
		if (grow_array((void **)&command->argv,
					   &command->argv_capacity, sizeof(*command->argv)))
			return -1;
	}

	new_argv = command->argv;
	new_argv[command->argc++] = argument;
	new_argv[command->argc] = NULL;
	
	return 0;
}

static int command_add_redirect(struct command *command,
								enum redirect_type type, char *path)
{
	if (!command || !path)
		return -1;	
	
	if (command->redirect_count == command->redirect_capacity) {
		if (grow_array((void **)&command->redirects,
					   &command->redirect_capacity,
					   sizeof(*command->redirects)))
			return -1;
	}
	
	command->redirects[command->redirect_count].type = type;
	command->redirects[command->redirect_count].path = path;
	command->redirect_count++;
	
	return 0;
}

static int pipeline_add_command(struct pipeline *pipeline,
								struct command *command)
{
	if (!pipeline || !command)
		return -1;
		
	if (pipeline->command_count == pipeline->command_capacity) {
		if (grow_array((void **)&pipeline->commands,
					   &pipeline->command_capacity,
					   sizeof(*pipeline->commands)))
			return -1;
	}
	
	pipeline->commands[pipeline->command_count] = *command;
	pipeline->command_count++;
	command_init(command);
	
	return 0;
}

static int lexer_next(struct lexer *lexer, struct token *token)
{
	size_t start;
	size_t length;
	size_t capacity;
	char *text;
	char quote;

	if (!lexer || !token)
		return -1;	
	
	token->type = TOKEN_END;
	token->text = NULL;
	
	while (isspace((unsigned char)lexer->input[lexer->position]) != 0)
		lexer->position++;
	
	if (lexer->input[lexer->position] == '\0')
		return 0;
	
	switch (lexer->input[lexer->position]) {
	case '|':
		token->type = TOKEN_PIPE;
		lexer->position++;
		return 0;
	case '&':
		token->type = TOKEN_BACKGROUND;
		lexer->position++;
		return 0;
	case '<':
		token->type = TOKEN_REDIRECT_INPUT;
		lexer->position++;
		return 0;
	case '>':
		lexer->position++;
		if (lexer->input[lexer->position] == '>') {
			token->type = TOKEN_REDIRECT_APPEND;
			lexer->position++;
		} else {
			token->type = TOKEN_REDIRECT_OUTPUT;
		}
		return 0;
	default:
		break;
	}
	
	capacity = 16;
	if (!(text = malloc(capacity)))
		return -1;
	
	length = 0;
	
	while (lexer->input[lexer->position] != '\0') {
		char current = lexer->input[lexer->position];

		if (isspace((unsigned char)current) != 0 ||
		    current == '|' || current == '&' ||
		    current == '<' || current == '>')
			break;

		if (current == '\'' || current == '"') {
			quote = current;
			lexer->position++;

			while (lexer->input[lexer->position] != '\0' &&
			       lexer->input[lexer->position] != quote) {
				if (length + 1 >= capacity) {
					capacity *= 2;
					if (!(text = realloc(text, capacity)))
						return -1;
				}
				
				text[length++] = lexer->input[lexer->position++];
			}
			
			if (lexer->input[lexer->position] != quote) {
				if (text)
					free(text);
				return -1;
			}
			
			lexer->position++;
			continue;
		}
		
		if (current == '\\') {
			lexer->position++;
			if (lexer->input[lexer->position] == '\0') {
				if (text)
					free(text);
				return -1;
			}
			
			current = lexer->input[lexer->position];
		}
		
		if (length + 1 >= capacity) {
			capacity *= 2;
			if (!(text = realloc(text, capacity)))
				return -1;
		}
		
		text[length++] = current;
		lexer->position++;
	}
	
	text[length] = '\0';
	start = 0;
	
	if (length == start) {
		if (text)
			free(text);
		return -1;
	}
	
	token->type = TOKEN_WORD;
	token->text = text;
	
	return 0;
}

static void token_free(struct token *token)
{
	if (!token)
		return;
	
	if (token->text)
		free(token->text);
	token->text = NULL;
}

static void parser_advance(struct parser *parser)
{
	if (!parser)
		return;
	
	token_free(&parser->current);
	
	if (lexer_next(&parser->lexer, &parser->current))
		parser->current.type = TOKEN_END;
}

static int  parse_command(struct parser *parser,
						  struct command *command)
{
	enum redirect_type redirect_type;
	char *path;

	if (!parser || !command)
		return -1;
	
	command_init(command);
	
	if (parser->current.type != TOKEN_WORD)
		return PARSE_ERROR_EMPTY_COMMAND;
	
	while (parser->current.type == TOKEN_WORD ||
	       parser->current.type == TOKEN_REDIRECT_INPUT ||
	       parser->current.type == TOKEN_REDIRECT_OUTPUT ||
	       parser->current.type == TOKEN_REDIRECT_APPEND) {
		if (parser->current.type == TOKEN_WORD) {
			char *argument = parser->current.text;
			
			parser->current.text = NULL;
			if (command_add_argument(command, argument)) {
				if (argument)
					free(argument);
				command_free(command);
				
				return PARSE_ERROR_OUT_OF_MEMORY;
			}
			
			parser_advance(parser);
			continue;
		}
		
		switch (parser->current.type) {
		case TOKEN_REDIRECT_INPUT:
			redirect_type = REDIRECT_INPUT;
			break;
		case TOKEN_REDIRECT_OUTPUT:
			redirect_type = REDIRECT_OUTPUT;
			break;
		case TOKEN_REDIRECT_APPEND:
			redirect_type = REDIRECT_APPEND;
			break;
		default:
			redirect_type = REDIRECT_INPUT;
			break;
		}
		
		parser_advance(parser);
		
		if (parser->current.type != TOKEN_WORD) {
			command_free(command);
			return PARSE_ERROR_MISSING_REDIRECT_TARGET;
		}
		
		path = parser->current.text;
		parser->current.text = NULL;
		
		if (command_add_redirect(command, redirect_type, path)) {
			if (path)
				free(path);
			
			command_free(command);
			return PARSE_ERROR_OUT_OF_MEMORY;
		}
		
		parser_advance(parser);
	}
	
	return PARSE_SUCCESS;
}

static int parse_pipeline(struct parser *parser,
						  struct pipeline *pipeline)
{
	struct command command;
	enum parse_error error;

	if (!parser || !pipeline)
		return -1;	
	
	pipeline_init(pipeline);

	error = parse_command(parser, &command);
	if (error != PARSE_SUCCESS)
		return error;
	
	if (pipeline_add_command(pipeline, &command)) {
		command_free(&command);
		pipeline_free(pipeline);
		return PARSE_ERROR_OUT_OF_MEMORY;
	}
		
	while (parser->current.type == TOKEN_PIPE) {
		parser_advance(parser);
		
		error = parse_command(parser, &command);
		if (error != PARSE_SUCCESS) {
			pipeline_free(pipeline);
			return error;
		}
		
		if (pipeline_add_command(pipeline, &command)) {
			command_free(&command);
			pipeline_free(pipeline);
			return PARSE_ERROR_OUT_OF_MEMORY;
		}
	}
	
	if (parser->current.type == TOKEN_BACKGROUND) {
		pipeline->background = 1;
		parser_advance(parser);
	}
	
	if (parser->current.type != TOKEN_END) {
		pipeline_free(pipeline);
		return PARSE_ERROR_UNEXPECTED_TOKEN;
	}
	
	return PARSE_SUCCESS;
}

int parse_line(const char *line, struct pipeline *pipeline)
{
	struct parser parser;
	
	if (!line || !pipeline)
		return -1;
	
	pipeline_init(pipeline);
	
	parser.lexer.input = line;
	parser.lexer.position = 0;
	parser.current.type = TOKEN_END;
	parser.current.text = NULL;
	
	if (lexer_next(&parser.lexer, &parser.current)) {
		token_free(&parser.current);
		return PARSE_SUCCESS;
		
		//return PARSE_ERROR_UNTERMINATED_QUOTE;
	}
	
	if (parser.current.type == TOKEN_END) {
		token_free(&parser.current);
		return PARSE_SUCCESS;		
		//return PARSE_ERROR_EMPTY_INPUT;
	}
	
	return parse_pipeline(&parser, pipeline);
}

const char *parse_error_string(int error)
{
	switch (error) {
	case PARSE_SUCCESS:
		return "success";
	case PARSE_ERROR_EMPTY_INPUT:
		return "empty input";
	case PARSE_ERROR_EMPTY_COMMAND:
		return "empty command";
	case PARSE_ERROR_UNTERMINATED_QUOTE:
		return "unterminated quote";
	case PARSE_ERROR_MISSING_REDIRECT_TARGET:
		return "missing redirect target";
	case PARSE_ERROR_UNEXPECTED_TOKEN:
		return "unexpected token";
	case PARSE_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	default:
		return "unknown parse error";
	}
}

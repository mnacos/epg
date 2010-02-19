/*  epg.c -- the C portion of epg -- an Erlang PostgreSQL libpq wrapper

      please read the associated LICENSE file
	  this program exposes libpq functionality to epg.erl
*/

/* revision information
   ----------------------------------------------------------------------
   Revision: 0.01
   Date: 2009/11/24 11:32:01
   Created by: m.nacos@gmail.com
  ---------------------------------------------------------------------- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <libpq-fe.h>

/* gcc -L/usr/local/pgsql8.4.1/lib -I/usr/local/pgsql8.4.1/include -lpq epg.c -o epg */

/* LINE_BUFFER_SIZE is the max number of bytes we allow per line */
#define LINE_BUFFER_SIZE 8192
#define HARD_LIMIT 32768

int run;
int last;
PGconn *connection;

/* simplistic die procedure */
void die (char *msg)
{
	fprintf(stdout,"%s\n!error!\n",msg);
	exit(1);
}

/* this function simply escapes all double quotes within a string */
static inline char * esc_dquotes(char * sometext)
{
    unsigned int size = strlen(sometext); int d_s = 2 * strlen(sometext);
    char *newtxt = (char *) malloc ( (d_s + 1) * sizeof(char) ); int k = 0; int i;
    for(i=0; i<size; i++) {
		if (sometext[i] == '\"') { strncpy(&newtxt[k++], "\\", 1); }
		strncpy(&newtxt[k++], &sometext[i], 1);
	}
    newtxt[k] = '\0';
    return newtxt;
}

/* this procedure handles stdin line by line */
char * rline()
{
	char *buffer = NULL;
	buffer = malloc(LINE_BUFFER_SIZE+1); buffer[0] = '\0';
	fgets(buffer, LINE_BUFFER_SIZE, stdin);
	buffer[LINE_BUFFER_SIZE] = '\0';

	// getting rid of trailing newline only if we have the whole input
	if (strlen(buffer) != LINE_BUFFER_SIZE-1 || buffer[strlen(buffer)-1] == '\n') {
		buffer[strlen(buffer)-1] = '\0';
	}
	// otheriwse we try to capture the rest, then proceed
	else {
		char *more = rline(); int mlen = strlen(more); int nl = strlen(buffer)+mlen+1;
		if (nl > HARD_LIMIT) {free(buffer);free(more);die("Input str limit reached!");}
		char *ptr = realloc(buffer,nl*sizeof(char));
		if (ptr != NULL) { buffer = ptr; strcat(buffer, more); free(more); }
		else { free(buffer); free(more); die("Out of memory!"); }
	}
	int size = strlen(buffer);
	if( last == 0 && size == 0 ) {
		run = 0; fprintf(stdout,"!bye!\n"); fflush(stdout);
	}
	last = size;

	// converting buffer to a wide-character string
	// wchar_t *conv = (wchar_t *) malloc(size * sizeof(wchar_t));
	// mbstowcs(conv,buffer,size); free(buffer);
	return buffer;
}
	
/* this procedure outputs the results of each query */
void wline (PGresult *res)
{
	int response = PQresultStatus(res);

	if (response == PGRES_EMPTY_QUERY) {
		fprintf(stdout,"!connected!\n");
		fflush(stdout); return;
	}
	else if (response == PGRES_COMMAND_OK) {
		fprintf(stdout,"%s\n!connected!\n", PQcmdStatus(res));
		fflush(stdout); return;
	}
	else if (response == PGRES_FATAL_ERROR) {
		fprintf(stdout,"%s!error!\n", PQresultErrorMessage(res));
		fflush(stdout); return;
	}
	else if (response == PGRES_BAD_RESPONSE) {
		fprintf(stdout,"%s!error!\n", PQresultErrorMessage(res));
		fflush(stdout); return;
	}

	int fields = PQnfields(res); int tuples = PQntuples(res); int i,k;
	fprintf(stdout,"[\n\t{header,\n\t\t[\n\t\t\t");
	for(k=0;k<fields;k++) {
		fprintf(stdout,"{\"%s\",%d}",PQfname(res,k), PQftype(res,k));
		if (k<fields-1) fprintf(stdout,",");
	}
	fprintf(stdout,"\n\t\t]\n\t},\n\n");
	fprintf(stdout,"\t{data,\n\t\t[\n");
	for(i=0;i<tuples;i++) {
		fprintf(stdout,"\t\t\t{");
		for(k=0;k<fields;k++) {
			if(PQgetisnull(res,i,k)) { fprintf(stdout,"\"%s\"","NULL"); }
			else {
				char *esc = esc_dquotes(PQgetvalue(res,i,k));
				fprintf(stdout,"\"%s\"", esc);
				free(esc);
			}
			if (k<fields-1) fprintf(stdout,",");
		}
		if (i<tuples-1) fprintf(stdout,"},\n"); else fprintf(stdout,"}\n");
	}
	fprintf(stdout,"\n\t\t]\n\t}\n]\n!eod!\n");
	fflush(stdout);
}

/* this procedure submits an SQL query to the server and calls wline */
char * xline(char *line)
{
	// simple interaction with Postgresql
	if( !run ) { return line; }
	if( !abs(strcmp(line,"quit")) ) {run = 0;fprintf(stdout,"!bye!\n");fflush(stdout);}
	else { PGresult *res = PQexec(connection, line); wline(res); PQclear(res); }
	return line;
}

/* this procedure takes a connection string and establishes db connection */
char * cline(char *line)
{
	// simple interaction with Postgresql
	if( !run ) { return line; }
	if( !abs(strcmp(line,"quit")) ) {
		run = 0; fprintf(stdout,"!bye!\n"); fflush(stdout);
		return line;	
	}
	else { connection = PQconnectdb(line); }
	if (PQstatus(connection) == CONNECTION_OK) { 
		char *pusr = PQuser(connection);
		char *phos = PQhost(connection);
		char *ppor = PQport(connection);
		char *pgdb = PQdb(connection);
		if (phos != NULL) { fprintf(stdout, "%s@%s:%s/%s\n", pusr, phos, ppor, pgdb); }
		else { fprintf(stdout, "%s@%s:%s/%s\n", pusr, "localhost", ppor, pgdb); }
		fprintf(stdout,"!connected!\n"); fflush(stdout);
	}
	else {
		fprintf(stdout,"%s!error!\n", PQerrorMessage(connection));
		PQfinish(connection); connection = NULL;
		fflush(stdout);
	}
	return line;
}

int main()
{
	run = 1; last = 1; connection = NULL;
	setlocale(LC_ALL,"");
	while(run && connection == NULL) { free(cline(rline())); }
	while(run) { free(xline(rline())); }
	if(connection != NULL) { PQfinish(connection); }
	return 0;
}


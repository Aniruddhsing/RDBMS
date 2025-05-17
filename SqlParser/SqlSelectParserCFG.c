#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <assert.h>

#include "ParserExport.h"
#include "SqlEnums.h"
#include "../core/qep.h"
#include "../core/SqlMexprIntf.h"
#include "../core/rdbms_struct.h"
#include "../core/Catalog.h"
#include "../core/sql_join.h"

extern BPlusTree_t TableCatalogDef;

qep_struct_t qep;

/*
select_query_parser -> select COLLIST from TABS

COLLIST -> COL | COL , COLLIST

COL -> < sql_create_exp_tree_compute () ALIAS >  

TABS -> <ident> ALIAS | <ident> ALIAS , TABS 

ALIAS -> as <identifier> | $ 
*/

static char alias_name[SQL_ALIAS_NAME_LEN];

//ALIAS -> as <identifier> | $ 
static parse_rc_t
ALIAS () {

    parse_init();

    memset(alias_name, 0, SQL_ALIAS_NAME_LEN);
    
    token_code = cyylex();

    if (token_code != SQL_AS) {
        yyrewind(1);
        RETURN_PARSE_SUCCESS;
    }

    token_code = cyylex();

    if (token_code != SQL_IDENTIFIER) {
        yyrewind(2);
        RETURN_PARSE_SUCCESS;
    }

    strncpy (alias_name, lex_curr_token, SQL_ALIAS_NAME_LEN - 1);

    RETURN_PARSE_SUCCESS;
}


//TABS -> <ident> ALIAS | <ident> ALIAS , TABS 
static parse_rc_t
TABS() {

    parse_init();
    int initial_chkp;

    CHECKPOINT(initial_chkp);

    //TABS ->  <ident> ALIAS , TABS 
    do {

        token_code = cyylex();

        if (token_code != SQL_IDENTIFIER) break;

        strncpy (qep.join.tables[qep.join.table_cnt].table_name, 
            lex_curr_token, SQL_TABLE_NAME_MAX_SIZE);

        memset(alias_name, 0, SQL_ALIAS_NAME_LEN);
        err = ALIAS();
        strncpy(qep.join.tables[qep.join.table_cnt].alias_name, alias_name, SQL_ALIAS_NAME_LEN - 1);

        token_code = cyylex();

        if (token_code != SQL_COMMA) {
            break;
        }

        qep.join.table_cnt++;

        err = TABS();

        if (err == PARSE_ERR) break;

        RETURN_PARSE_SUCCESS;
        
    } while (0);

    RESTORE_CHKP(initial_chkp);

   //TABS -> <ident> ALIAS
    token_code = cyylex();

    if (token_code != SQL_IDENTIFIER) {
        PARSER_LOG_ERR(token_code, SQL_IDENTIFIER);
        RETURN_PARSE_ERROR;
    }
    
    strncpy (qep.join.tables[qep.join.table_cnt].table_name, 
            lex_curr_token, SQL_TABLE_NAME_MAX_SIZE);

    memset(alias_name, 0, SQL_ALIAS_NAME_LEN);
    err = ALIAS();
    strncpy(qep.join.tables[qep.join.table_cnt].alias_name, alias_name, SQL_ALIAS_NAME_LEN - 1);
    qep.join.table_cnt++;

    RETURN_PARSE_SUCCESS;
}


// Helper to expand * to all columns of all tables in FROM clause
static void expand_wildcard_columns() {
    // First load table catalog information
    if (!sql_query_initialize_join_clause(&qep, &TableCatalogDef)) {
        printf("Error: Failed to load table catalog information\n");
        return;
    }
    int t;
    for (t = 0; t < qep.join.table_cnt && t < SQL_MAX_TABLES_IN_JOIN_LIST; t++) {
        ctable_val_t *ctable_val = qep.join.tables[t].ctable_val;
        if (!ctable_val) continue;
        for (int c = 0; c < SQL_MAX_COLUMNS_SUPPORTED_PER_TABLE && ctable_val->column_lst[c][0] != '\0'; c++) {
            const char *col_name = ctable_val->column_lst[c];
            if (!col_name) continue;
            qp_col_t *qp_col = (qp_col_t *)calloc(1, sizeof(qp_col_t));
            if (!qp_col) continue;
            qp_col->sql_tree = sql_create_exp_tree_for_column(col_name);
            if (!qp_col->sql_tree) {
                free(qp_col);
                continue;
            }
            if (qep.select.n >= SQL_MAX_COLS_IN_SELECT_LIST) {
                sql_destroy_exp_tree(qp_col->sql_tree);
                free(qp_col);
                break;
            }
            qep.select.sel_colmns[qep.select.n] = qp_col;
            qep.select.n++;
        }
    }
}

// COL -> < sql_create_exp_tree_compute () ALIAS > | *  
static parse_rc_t 
COL() {
    parse_init();
    token_code = cyylex();
    // Check for wildcard
    if (token_code == SQL_WILDCARD) {
        // Initialize qep structure for wildcard expansion
        memset(&qep, 0, sizeof(qep));
        expand_wildcard_columns();
        RETURN_PARSE_SUCCESS;
    }
    // Not a wildcard, rewind and handle as normal column
    yyrewind(1);
    qp_col_t *qp_col = (qp_col_t *)calloc(1, sizeof(qp_col_t));
    if (!qp_col) {
        printf("Error: Failed to allocate memory for column\n");
        RETURN_PARSE_ERROR;
    }
    qep.select.sel_colmns[qep.select.n] = qp_col;
    qep.select.n++;
    qp_col->sql_tree = sql_create_exp_tree_compute();
    if (!qp_col->sql_tree) {
        free(qp_col);
        qep.select.n--;
        qep.select.sel_colmns[qep.select.n] = NULL;
        RETURN_PARSE_ERROR;   
    }
    memset(alias_name, 0, SQL_ALIAS_NAME_LEN);
    err = ALIAS();
    strncpy(qp_col->alias_name, alias_name, SQL_ALIAS_NAME_LEN - 1);
    if (qp_col->alias_name[0] != '\0') {
        qp_col->alias_provided_by_user = true;
    }
    RETURN_PARSE_SUCCESS;
}


// COLLIST -> COL | COL , COLLIST
static  parse_rc_t 
 COLLIST() {

    parse_init();
    int initial_chkp;

    CHECKPOINT(initial_chkp);

    // COLLIST -> COL , COLLIST
    do {

        err = COL();

        if (err == PARSE_ERR) break;

        token_code = cyylex();

        if (token_code != SQL_COMMA) {
            
            qep.select.n--;
            sql_destroy_exp_tree(qep.select.sel_colmns[qep.select.n]->sql_tree);
            free(qep.select.sel_colmns[qep.select.n]);
            qep.select.sel_colmns[qep.select.n] = NULL;
            break;
        }

        err = COLLIST();

        if (err == PARSE_ERR) break;

        RETURN_PARSE_SUCCESS;

    } while (0);

    RESTORE_CHKP(initial_chkp);

    // COLLIST -> COL 

    err = COL();

    if (err == PARSE_ERR) RETURN_PARSE_ERROR;

    RETURN_PARSE_SUCCESS;
 }


// select_query_parser -> select COLLIST from TABS
parse_rc_t 
select_query_parser() {

    parse_init();

    // Initialize qep structure
    memset(&qep, 0, sizeof(qep));

    token_code = cyylex();

    assert(token_code == SQL_SELECT_Q);

    err = COLLIST();

    if (err == PARSE_ERR) {
        // Clean up on error
        for (int i = 0; i < qep.select.n; i++) {
            if (qep.select.sel_colmns[i]) {
                if (qep.select.sel_colmns[i]->sql_tree) {
                    sql_destroy_exp_tree(qep.select.sel_colmns[i]->sql_tree);
                }
                free(qep.select.sel_colmns[i]);
            }
        }
        RETURN_PARSE_ERROR;
    }

    token_code = cyylex();

    if (token_code != SQL_FROM) {
        // Clean up on error
        for (int i = 0; i < qep.select.n; i++) {
            if (qep.select.sel_colmns[i]) {
                if (qep.select.sel_colmns[i]->sql_tree) {
                    sql_destroy_exp_tree(qep.select.sel_colmns[i]->sql_tree);
                }
                free(qep.select.sel_colmns[i]);
            }
        }
        RETURN_PARSE_ERROR;
    }

    err = TABS();

    if (err == PARSE_ERR) {
        // Clean up on error
        for (int i = 0; i < qep.select.n; i++) {
            if (qep.select.sel_colmns[i]) {
                if (qep.select.sel_colmns[i]->sql_tree) {
                    sql_destroy_exp_tree(qep.select.sel_colmns[i]->sql_tree);
                }
                free(qep.select.sel_colmns[i]);
            }
        }
        RETURN_PARSE_ERROR;
    }

    token_code = cyylex();

    if (token_code != PARSER_EOL) {
        PARSER_LOG_ERR(token_code, PARSER_EOL);
        // Clean up on error
        for (int i = 0; i < qep.select.n; i++) {
            if (qep.select.sel_colmns[i]) {
                if (qep.select.sel_colmns[i]->sql_tree) {
                    sql_destroy_exp_tree(qep.select.sel_colmns[i]->sql_tree);
                }
                free(qep.select.sel_colmns[i]);
            }
        }
        RETURN_PARSE_ERROR;
    }

    RETURN_PARSE_SUCCESS;
}
//===- SaberCheckerAPI.cpp -- API for checkers-------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013->  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * SaberCheckerAPI.cpp
 *
 *  Created on: Apr 23, 2014
 *      Author: Yulei Sui
 */
#include "SABER/SaberCheckerAPI.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace std;
using namespace SVF;

SaberCheckerAPI* SaberCheckerAPI::ckAPI = nullptr;

namespace
{

/// string and type pair
struct ei_pair
{
    const char *n;
    SaberCheckerAPI::CHECKER_TYPE t;
};

} // End anonymous namespace

static string trimAPIConfigToken(const string& token)
{
    string::size_type begin = 0;
    string::size_type end = token.size();
    while(begin < end && !isalnum(static_cast<unsigned char>(token[begin])) && token[begin] != '_')
        ++begin;
    while(end > begin && !isalnum(static_cast<unsigned char>(token[end - 1])) && token[end - 1] != '_')
        --end;
    return token.substr(begin, end - begin);
}

static string normalizeAPIConfigToken(string token)
{
    transform(token.begin(), token.end(), token.begin(),
              [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
    return token;
}

static bool parseCheckerTypeToken(const string& token, SaberCheckerAPI::CHECKER_TYPE& type)
{
    const string normalized = normalizeAPIConfigToken(token);
    if(normalized == "ck_alloc" || normalized == "alloc" ||
            normalized == "allocator" || normalized == "malloc")
    {
        type = SaberCheckerAPI::CK_ALLOC;
        return true;
    }
    if(normalized == "ck_free" || normalized == "free" ||
            normalized == "releaser" || normalized == "destroyer" ||
            normalized == "dealloc" || normalized == "deallocator")
    {
        type = SaberCheckerAPI::CK_FREE;
        return true;
    }
    if(normalized == "ck_fopen" || normalized == "fopen" ||
            normalized == "fileopen" || normalized == "file_open")
    {
        type = SaberCheckerAPI::CK_FOPEN;
        return true;
    }
    if(normalized == "ck_fclose" || normalized == "fclose" ||
            normalized == "fileclose" || normalized == "file_close")
    {
        type = SaberCheckerAPI::CK_FCLOSE;
        return true;
    }
    return false;
}

//Each (name, type) pair will be inserted into the map.
//All entries of the same type must occur together (for error detection).
static const ei_pair ei_pairs[]=
{
    {"alloc", SaberCheckerAPI::CK_ALLOC},
    {"alloc_check", SaberCheckerAPI::CK_ALLOC},
    {"alloc_clear", SaberCheckerAPI::CK_ALLOC},
    {"calloc", SaberCheckerAPI::CK_ALLOC},
    {"jpeg_alloc_huff_table", SaberCheckerAPI::CK_ALLOC},
    {"jpeg_alloc_quant_table", SaberCheckerAPI::CK_ALLOC},
    {"lalloc", SaberCheckerAPI::CK_ALLOC},
    {"lalloc_clear", SaberCheckerAPI::CK_ALLOC},
    {"malloc", SaberCheckerAPI::CK_ALLOC},
    {"nhalloc", SaberCheckerAPI::CK_ALLOC},
    {"oballoc", SaberCheckerAPI::CK_ALLOC},
    {"permalloc", SaberCheckerAPI::CK_ALLOC},
    {"png_create_info_struct", SaberCheckerAPI::CK_ALLOC},
    {"png_create_write_struct", SaberCheckerAPI::CK_ALLOC},
    {"safe_calloc", SaberCheckerAPI::CK_ALLOC},
    {"safe_malloc", SaberCheckerAPI::CK_ALLOC},
    {"safecalloc", SaberCheckerAPI::CK_ALLOC},
    {"safemalloc", SaberCheckerAPI::CK_ALLOC},
    {"safexcalloc", SaberCheckerAPI::CK_ALLOC},
    {"safexmalloc", SaberCheckerAPI::CK_ALLOC},
    {"savealloc", SaberCheckerAPI::CK_ALLOC},
    {"xalloc", SaberCheckerAPI::CK_ALLOC},
    {"xcalloc", SaberCheckerAPI::CK_ALLOC},
    {"xmalloc", SaberCheckerAPI::CK_ALLOC},
    {"SSL_CTX_new", SaberCheckerAPI::CK_ALLOC},
    {"SSL_new", SaberCheckerAPI::CK_ALLOC},
    {"VOS_MemAlloc", SaberCheckerAPI::CK_ALLOC},
    /* NSPA_AUTO_BASH_CK_ALLOC_BEGIN */
    /* NSPA: bash project-specific CK_ALLOC entries */
    {"_rl_callback_data_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/readline/callback.c
    {"_rl_copy_undo_entry", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/readline/undo.c
    {"_rl_keyseq_cxt_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, lib/readline/readline.c
    {"_rl_make_prompt_for_search", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/readline/display.c
    {"_rl_mvcxt_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/readline/vi_mode.c
    {"_rl_scxt_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, lib/readline/isearch.c
    {"alloc_history_entry", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, lib/readline/history.c
    {"alloc_lvalue", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, expr.c
    {"alloc_pipeline_saver", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, jobs.c
    {"alloc_undo_entry", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, lib/readline/undo.c
    {"alloc_word_desc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, make_cmd.c
    {"alloca", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/malloc/alloca.c
    {"array_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, array.c
    {"compspec_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, pcomplib.c
    {"copy_history_entry", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/readline/history.c
    {"expand_prompt", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/readline/display.c
    {"get_bash_name", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, variables.c
    {"indirection_level_string", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, print_cmd.c
    {"make_bare_simple_command", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, make_cmd.c
    {"make_default_mailpath", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, mailcheck.c
    {"make_func_export_array", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.75, variables.c
    {"make_named_pipe", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, subst.c
    {"normalize_codeset", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/readline/nls.c
    {"read_man_page", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, support/man2html.c
    {"realloc_line", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.7, lib/readline/display.c
    {"remove_duplicate_matches", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.7, lib/readline/complete.c
    {"remove_history_range", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.7, lib/readline/history.c
    {"rl_copy_text", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/readline/util.c
    {"rl_funmap_names", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/readline/funmap.c
    {"rl_gets", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.75, lib/readline/examples/manexamp.c
    {"rl_make_bare_keymap", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, lib/readline/keymaps.c
    {"sh_getopt_alloc_istate", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, builtins/getopt.c
    {"stralloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, support/man2html.c
    {"strduplicate", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, support/man2html.c
    {"strgrow", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, support/man2html.c
    {"strlist_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/sh/stringlist.c
    {"strvec_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, lib/sh/stringvec.c
    {"tilde_expand_word", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/readline/tilde.c
    {"tilde_find_word", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/readline/tilde.c
    {"xdupmbstowcs", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/glob/xmbsrtowcs.c
    {"xdupmbstowcs2", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, lib/glob/xmbsrtowcs.c
    {"xrealloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, lib/readline/tilde.c
    /* NSPA_AUTO_BASH_CK_ALLOC_END */






















    {"VOS_MemFree", SaberCheckerAPI::CK_FREE},
    {"cfree", SaberCheckerAPI::CK_FREE},
    {"free", SaberCheckerAPI::CK_FREE},
    {"free_all_mem", SaberCheckerAPI::CK_FREE},
    {"freeaddrinfo", SaberCheckerAPI::CK_FREE},
    {"gcry_mpi_release", SaberCheckerAPI::CK_FREE},
    {"gcry_sexp_release", SaberCheckerAPI::CK_FREE},
    {"globfree", SaberCheckerAPI::CK_FREE},
    {"nhfree", SaberCheckerAPI::CK_FREE},
    {"obstack_free", SaberCheckerAPI::CK_FREE},
    {"safe_cfree", SaberCheckerAPI::CK_FREE},
    {"safe_free", SaberCheckerAPI::CK_FREE},
    {"safefree", SaberCheckerAPI::CK_FREE},
    {"safexfree", SaberCheckerAPI::CK_FREE},
    {"sm_free", SaberCheckerAPI::CK_FREE},
    {"vim_free", SaberCheckerAPI::CK_FREE},
    {"xfree", SaberCheckerAPI::CK_FREE},
    {"SSL_CTX_free", SaberCheckerAPI::CK_FREE},
    {"SSL_free", SaberCheckerAPI::CK_FREE},
    {"XFree", SaberCheckerAPI::CK_FREE},
    /* NSPA_AUTO_BASH_CK_FREE_BEGIN */
    /* NSPA: bash project-specific CK_FREE entries */
    {"_nl_free_domain_conv", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, lib/intl/loadmsgcat.c
    {"_rl_callback_data_dispose", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, lib/readline/callback.c
    {"_rl_free_history_entry", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, lib/readline/misc.c
    {"_rl_free_match_list", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, lib/readline/complete.c
    {"_rl_free_saved_history_line", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, lib/readline/misc.c
    {"_rl_free_undo_list", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, lib/readline/undo.c
    {"_rl_keyseq_chain_dispose", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, lib/readline/readline.c
    {"_rl_keyseq_cxt_dispose", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, lib/readline/readline.c
    {"_rl_mvcxt_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, lib/readline/vi_mode.c
    {"_rl_scxt_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, lib/readline/isearch.c
    {"array_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, array.c
    {"array_dispose_element", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, array.c
    {"array_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.6, builtins/mkbuiltins.c
    {"assoc_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, assoc.c
    {"bash_delete_histent", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, bashhist.c
    {"bash_delete_history_range", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, bashhist.c
    {"bash_delete_last_history", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, bashhist.c
    {"bgp_clear", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, jobs.c
    {"clean_itemlist", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, pcomplete.c
    {"clear_fifo_list", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, subst.c
    {"clear_hostname_list", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, bashline.c
    {"clear_table", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, support/man2html.c
    {"compspec_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, pcomplib.c
    {"coproc_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, execute_cmd.c
    {"coproc_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, execute_cmd.c
    {"cpe_dispose", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, execute_cmd.c
    {"delete_all_aliases", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, alias.c
    {"delete_all_contexts", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, variables.c
    {"delete_all_jobs", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, jobs.c
    {"delete_all_variables", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, variables.c
    {"delete_job", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, jobs.c
    {"delete_old_job", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.6, jobs.c
    {"delete_var", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, variables.c
    {"discard_pipeline", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, jobs.c
    {"dispose_command", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, dispose_cmd.c
    {"dispose_cond_node", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, dispose_cmd.c
    {"dispose_exec_redirects", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, execute_cmd.c
    {"dispose_fd_bitmap", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.75, execute_cmd.c
    {"dispose_function_def", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, dispose_cmd.c
    {"dispose_function_def_contents", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, dispose_cmd.c
    {"dispose_mail_file", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, mailcheck.c
    {"dispose_partial_redirects", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, execute_cmd.c
    {"dispose_redirects", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, dispose_cmd.c
    {"dispose_saved_dollar_vars", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, variables.c
    {"dispose_temporary_env", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, variables.c
    {"dispose_used_env_vars", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.65, variables.c
    {"dispose_var_context", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, variables.c
    {"dispose_variable", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, variables.c
    {"dispose_variable_value", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, variables.c
    {"dispose_word", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, dispose_cmd.c
    {"dispose_word_array", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, dispose_cmd.c
    {"dispose_word_desc", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, dispose_cmd.c
    {"dispose_words", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, dispose_cmd.c
    {"free_alias_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, alias.c
    {"free_buffered_stream", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.75, input.c
    {"free_builtin", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, builtins/mkbuiltins.c
    {"free_defs", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, builtins/mkbuiltins.c
    {"free_dollar_vars", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, variables.c
    {"free_history_entry", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, lib/readline/history.c
    {"free_lvalue", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, expr.c
    {"free_mail_files", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, mailcheck.c
    {"free_mem", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, lib/intl/dcigettext.c
    {"free_progcomp", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, pcomplib.c
    {"free_pushed_string_input", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, y.tab.c
    {"free_saved_dollar_vars", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, variables.c
    {"free_string_list", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, y.tab.c
    {"free_trap_command", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, trap.c
    {"free_trap_string", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, trap.c
    {"free_trap_strings", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, trap.c
    {"free_undo_list", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, lib/readline/compat.c
    {"free_variable_hash_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, variables.c
    {"freewords", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, lib/readline/histexpand.c
    {"hash_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, hashlib.c
    {"hash_flush", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, hashlib.c
    {"process_line", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, lib/readline/examples/excallback.c
    {"procsub_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, jobs.c
    {"progcomp_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, pcomplib.c
    {"rl_discard_keymap", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, lib/readline/keymaps.c
    {"rl_free_keymap", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, lib/readline/keymaps.c
    {"rl_free_undo_list", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, lib/readline/undo.c
    {"set_up_new_line", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, bashline.c
    {"sh_getopt_dispose_istate", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.6, builtins/getopt.c
    {"strlist_dispose", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, lib/sh/stringlist.c
    {"strvec_dispose", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.7, lib/sh/stringvec.c
    {"strvec_flush", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, lib/sh/stringvec.c
    /* NSPA_AUTO_BASH_CK_FREE_END */






















    {"fopen", SaberCheckerAPI::CK_FOPEN},
    {"\01_fopen", SaberCheckerAPI::CK_FOPEN},
    {"\01fopen64", SaberCheckerAPI::CK_FOPEN},
    {"\01readdir64", SaberCheckerAPI::CK_FOPEN},
    {"\01tmpfile64", SaberCheckerAPI::CK_FOPEN},
    {"fopen64", SaberCheckerAPI::CK_FOPEN},
    {"XOpenDisplay", SaberCheckerAPI::CK_FOPEN},
    {"XtOpenDisplay", SaberCheckerAPI::CK_FOPEN},
    {"fopencookie", SaberCheckerAPI::CK_FOPEN},
    {"popen", SaberCheckerAPI::CK_FOPEN},
    {"readdir", SaberCheckerAPI::CK_FOPEN},
    {"readdir64", SaberCheckerAPI::CK_FOPEN},
    {"gzdopen", SaberCheckerAPI::CK_FOPEN},
    {"iconv_open", SaberCheckerAPI::CK_FOPEN},
    {"tmpfile", SaberCheckerAPI::CK_FOPEN},
    {"tmpfile64", SaberCheckerAPI::CK_FOPEN},
    {"BIO_new_socket", SaberCheckerAPI::CK_FOPEN},
    {"gcry_md_open", SaberCheckerAPI::CK_FOPEN},
    {"gcry_cipher_open", SaberCheckerAPI::CK_FOPEN},


    {"fclose", SaberCheckerAPI::CK_FCLOSE},
    {"XCloseDisplay", SaberCheckerAPI::CK_FCLOSE},
    {"XtCloseDisplay", SaberCheckerAPI::CK_FCLOSE},
    {"__res_nclose", SaberCheckerAPI::CK_FCLOSE},
    {"pclose", SaberCheckerAPI::CK_FCLOSE},
    {"closedir", SaberCheckerAPI::CK_FCLOSE},
    {"dlclose", SaberCheckerAPI::CK_FCLOSE},
    {"gzclose", SaberCheckerAPI::CK_FCLOSE},
    {"iconv_close", SaberCheckerAPI::CK_FCLOSE},
    {"gcry_md_close", SaberCheckerAPI::CK_FCLOSE},
    {"gcry_cipher_close", SaberCheckerAPI::CK_FCLOSE},

    //This must be the last entry.
    {0, SaberCheckerAPI::CK_DUMMY}

};


/*!
 * initialize the map
 */
void SaberCheckerAPI::init()
{
    set<CHECKER_TYPE> t_seen;
    CHECKER_TYPE prev_t= CK_DUMMY;
    t_seen.insert(CK_DUMMY);
    for(const ei_pair *p= ei_pairs; p->n; ++p)
    {
        if(p->t != prev_t)
        {
            //This will detect if you move an entry to another block
            //  but forget to change the type.
            if(t_seen.count(p->t))
            {
                fputs(p->n, stderr);
                putc('\n', stderr);
                assert(!"ei_pairs not grouped by type");
            }
            t_seen.insert(p->t);
            prev_t= p->t;
        }
        if(tdAPIMap.count(p->n))
        {
            fputs(p->n, stderr);
            putc('\n', stderr);
            assert(!"duplicate name in ei_pairs");
        }
        tdAPIMap[p->n]= p->t;
    }
}

void SaberCheckerAPI::addCustomAPI(const string& funcName, CHECKER_TYPE type)
{
    if(funcName.empty() || type == CK_DUMMY)
        return;
    tdAPIMap[funcName] = type;
}

bool SaberCheckerAPI::loadCustomAPIsFromFile(const string& filename)
{
    ifstream input(filename.c_str());
    if(!input)
    {
        fprintf(stderr, "Cannot open custom API config file: %s\n", filename.c_str());
        return false;
    }

    string line;
    unsigned lineNo = 0;
    while(getline(input, line))
    {
        ++lineNo;

        string::size_type hash = line.find('#');
        if(hash != string::npos)
            line.erase(hash);
        string::size_type slash = line.find("//");
        if(slash != string::npos)
            line.erase(slash);

        for(char& ch : line)
        {
            if(ch == ':' || ch == '=' || ch == ',' || ch == ';' ||
                    ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
                    ch == '(' || ch == ')' || ch == '"' || ch == '\'' ||
                    isspace(static_cast<unsigned char>(ch)))
            {
                ch = ' ';
            }
        }

        vector<string> tokens;
        string token;
        stringstream stream(line);
        while(stream >> token)
        {
            token = trimAPIConfigToken(token);
            if(!token.empty())
                tokens.push_back(token);
        }
        if(tokens.empty())
            continue;

        CHECKER_TYPE type = CK_DUMMY;
        size_t typeIndex = tokens.size();
        for(size_t i = 0; i < tokens.size(); ++i)
        {
            if(parseCheckerTypeToken(tokens[i], type))
            {
                typeIndex = i;
                break;
            }
        }

        if(type == CK_DUMMY)
        {
            fprintf(stderr, "Ignore custom API config line %u without checker type.\n", lineNo);
            continue;
        }

        for(size_t i = 0; i < tokens.size(); ++i)
        {
            if(i == typeIndex)
                continue;
            const string normalized = normalizeAPIConfigToken(tokens[i]);
            if(normalized == "name" || normalized == "names" ||
                    normalized == "function" || normalized == "functions" ||
                    normalized == "category" || normalized == "checker" ||
                    normalized == "checker_type" || normalized == "type")
            {
                continue;
            }
            addCustomAPI(tokens[i], type);
        }
    }

    return true;
}



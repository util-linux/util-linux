#ifndef MAKEDEV_H
#define MAKEDEV_H

void init_parse(void);
void parse(void);
typedef union {
  int alignment;
  char ag_vt_2[sizeof(int)];
  char ag_vt_3[sizeof(char)];
  char ag_vt_4[sizeof(batch *)];
  char ag_vt_5[sizeof(const char *)];
} parse_vs_type;

typedef enum {
  parse_white_space_token = 2, parse_file_format_token = 5,
  parse_devices_token = 7, parse_cache_token = 9, parse_devinfo_token = 11,
  parse_config_token = 13, parse_eol_token, parse_device_list_token = 16,
  parse_eof_token = 18, parse_character_device_token = 20,
  parse_block_device_token = 23, parse_number_token = 25, parse_name_token,
  parse_cachedevice_token, parse_devicetype_token = 29,
  parse_device_block_token = 34, parse_device_header_spec_token = 36,
  parse_device_decl_token = 38, parse_ignoramus_token = 43,
  parse_batch_list_token = 46, parse_batch_item_token,
  parse_groupname_token = 51, parse_procname_token = 53,
  parse_class_token = 55, parse_device_tail_token, parse_expr_token = 58,
  parse_device_range_token, parse_hex_number_token = 63,
  parse_auto_hex_token, parse_devname_token, parse_config_decl_token = 69,
  parse_class_decl_token = 71, parse_omit_decl_token, parse_mode_token = 74,
  parse_single_omit_token = 76, parse_octal_number_token = 78,
  parse_qstring_token = 96, parse_qstring_char_token, parse_qchar_token,
  parse_hex_digit_token = 104, parse_term_token = 107,
  parse_factor_token = 109, parse_letter_token = 136,
  parse_simple_eol_token = 140, parse_identifier_token,
  parse_quoted_string_token, parse_digit_token,
  parse_octal_digit_token = 147
} parse_token_type;

typedef struct {
  parse_token_type token_number, reduction_token, error_frame_token;
  int input_code;
  int input_value;
  int line, column;
  int ssx, sn, error_frame_ssx;
  int drt, dssx, dsn;
  int ss[38];
  parse_vs_type vs[38];
  int bts[38], btsx;
  unsigned char * pointer;
  unsigned char * la_ptr;
  int lab[19], rx, fx;
  unsigned char *key_sp;
  int save_index, key_state;
  char *error_message;
  char read_flag;
  char exit_flag;
} parse_pcb_type;


#ifndef PRULE_CONTEXT
#define PRULE_CONTEXT(pcb)  (&((pcb).cs[(pcb).ssx]))
#define PERROR_CONTEXT(pcb) ((pcb).cs[(pcb).error_frame_ssx])
#define PCONTEXT(pcb)       ((pcb).cs[(pcb).ssx])
#endif


#ifndef AG_RUNNING_CODE_CODE
/* PCB.exit_flag values */
#define AG_RUNNING_CODE         0
#define AG_SUCCESS_CODE         1
#define AG_SYNTAX_ERROR_CODE    2
#define AG_REDUCTION_ERROR_CODE 3
#define AG_STACK_ERROR_CODE     4
#define AG_SEMANTIC_ERROR_CODE  5
#endif

extern parse_pcb_type parse_pcb;
#endif


#include "diag.h"

#include <string.h>

static diag_state_t s_state;

void diag_record_error(const char *module, const char *code)
{
    strncpy(s_state.module, module, DIAG_MODULE_MAX_LEN);
    s_state.module[DIAG_MODULE_MAX_LEN] = '\0';
    strncpy(s_state.code, code, DIAG_CODE_MAX_LEN);
    s_state.code[DIAG_CODE_MAX_LEN] = '\0';
    s_state.has_error = true;
    s_state.seq++;
}

void diag_clear(void)
{
    s_state.has_error = false;
    s_state.module[0] = '\0';
    s_state.code[0] = '\0';
    s_state.seq++;
}

const diag_state_t *diag_get(void)
{
    return &s_state;
}

/**
 * @file: main_write_callback.h
 * @brief: Curl functions
 */

#pragma once

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
int recv_buf_init(RECV_BUF *ptr, size_t max_size)
int recv_buf_cleanup(RECV_BUF *ptr)
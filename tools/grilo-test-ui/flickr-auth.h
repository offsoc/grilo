/*
 * Copyright (C) 2010 Igalia S.L.
 *
 * Contact: Iago Toral Quiroga <itoral@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef _FLICKR_AUTH_H_
#define _FLICKR_AUTH_H_

#include <glib.h>

gchar *
flickr_get_frob (const gchar *key,
                 const gchar *secret);

gchar *
flickr_get_token (const gchar *key,
                  const gchar *secret,
                  const gchar *frob);

gchar *
flickr_get_login_link (const gchar *key,
                       const gchar *secret,
                       const gchar *frob,
                       const gchar *perm);

#endif /* _FLICKR_AUTH_H_ */

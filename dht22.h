/*
 * DHT22 Humidity And Temperature Sensor Driver
 * 
 * Copyright (c) Edward Lin <edwardlin.tw@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef _DHT22_H
#define _DHT22_H

#define DEFAULT_GPIO            4
#define DEFAULT_AUTOUPDATE_SEC  10          /* re-trigger DHT22 after 10 sec */
#define AUTOUPDATE_SEC_MIN      3           /* 3 seconds */
#define AUTOUPDATE_SEC_MAX      60000       /* 10 min */

#define IO_BUF_MAX          64

/*
 * proprietary to dht22.c, not seen by others
 */
#ifdef _INCLUDE_DHT22_DECL

struct dht22_data {
    char        buf[IO_BUF_MAX];    
    rwlock_t    lock;
};


static void process_results(struct work_struct* work);
static irqreturn_t dht22_irq_handler(int irq, void* data); 
static enum hrtimer_restart autoupdate_func(struct hrtimer *hrtimer);
static enum hrtimer_restart timeout_func(struct hrtimer* hrtimer);
static void to_trigger_dht22(void);
static void trigger_dht22(void);
static void init_dht22_timer(struct hrtimer*, 
                             enum hrtimer_restart (*)(struct hrtimer*),
                             bool,
                             int);

static int      init_dht22_dev(void);
static void     exit_dht22_dev(void);
static int      dev_open(struct inode*, struct file*);
static int      dev_close(struct inode*, struct file*);
static ssize_t  dev_read(struct file*, char __user*, size_t, loff_t*);

#define ATTR_RW(v) struct kobj_attribute v ## _attr = __ATTR_RW(v)
#define ATTR_RO(v) struct kobj_attribute v ## _attr = __ATTR_RO(v)
#define ATTR_WO(v) struct kobj_attribute v ## _attr = __ATTR_WO(v)

#define DECL_ATTR_SHOW(f)  ssize_t f ## _show (struct kobject* kobj,\
                                               struct kobj_attribute* attr,\
                                               char* buf)

#define DECL_ATTR_STORE(f) ssize_t f ## _store(struct kobject* kobj,\
                                               struct kobj_attribute* attr,\
                                               const char* buf,\
                                               size_t count)

static DECL_ATTR_SHOW (gpio);
static DECL_ATTR_SHOW (autoupdate);
static DECL_ATTR_STORE(autoupdate);
static DECL_ATTR_SHOW (autoupdate_sec);
static DECL_ATTR_STORE(autoupdate_sec);
static DECL_ATTR_SHOW (humidity);
static DECL_ATTR_SHOW (temperature);
static DECL_ATTR_STORE(trigger);
static DECL_ATTR_STORE(debug);

#endif /* _INCLUDE_DHT22_DECL */

#endif /*_DHT22_H */


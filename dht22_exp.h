#ifndef _DHT22_EXP_H
#define _DHT22_EXP_H

#define DEFAULT_GPIO            4
#define DEFAULT_AUTOUPDATE_SEC  10    /* re-trigger DHT22 after 10 sec */
#define AUTOUPDATE_SEC_MIN      3     /* 3 seconds */
#define AUTOUPDATE_SEC_MAX      60000 /* 10 min */

/*
 * proprietary to dht22.c, not seen by others
 */
#ifdef _INCLUDE_DHT22_EXP_DECL

static void process_results(struct work_struct* work);
static irqreturn_t dht22_irq_handler(int irq, void* data); 
static enum hrtimer_restart autoupdate_func(struct hrtimer *hrtimer);
static enum hrtimer_restart timeout_func(struct hrtimer* hrtimer);
static void to_trigger_dht22(void);
static void trigger_dht22(void);

#define DECL_ATTR_SHOW(F)  ssize_t F ## _show (struct kobject* kobj,\
                                               struct kobj_attribute* attr,\
                                               char* buf)

#define DECL_ATTR_STORE(F) ssize_t F ## _store(struct kobject* kobj,\
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

#endif /* _INCLUDE_DHT22_EXP_DECL */

#endif /*_DHT22_EXP_H */


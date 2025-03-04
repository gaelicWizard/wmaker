
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#include "wconfig.h"

#include "WINGs.h"
#include "WINGsP.h"
#include "userdefaults.h"


typedef struct W_UserDefaults {
	WMPropList *defaults;

	WMPropList *appDomain;

	WMPropList *searchListArray;
	WMPropList **searchList;	/* cache for searchListArray */

	char dirty;

	char dontSync;

	char *path;		/* where is db located */

	time_t timestamp;	/* last modification time */

	struct W_UserDefaults *next;

} UserDefaults;

static UserDefaults *sharedUserDefaults = NULL;

char *WMUserDefaultsDidChangeNotification = "WMUserDefaultsDidChangeNotification";

static void synchronizeUserDefaults(void *foo);

#ifndef HAVE_INOTIFY
/* Check defaults database for changes every this many milliseconds */
/* XXX: this is shared with src/ stuff, put it in some common header */
#define UD_SYNC_INTERVAL	2000
#endif

const char *wusergnusteppath()
{
	static char *path = NULL;
	char *gspath = NULL;

	if (path)
		/* Value have been already computed, re-use it */
		return path;

	if (gspath = GETENV("GNUSTEP_USER_ROOT"))
		if (path = wglobpath(gspath)) {
			wwarning(_("variable GNUSTEP_USER_ROOT is deprecated; use GNUSTEP_USER_DEFAULTS_DIR or XDG_CONFIG_HOME"));
puts("GNUSTEP_USER_ROOT");
puts(path);
			return path;
		}

	if (gspath = GETENV("WMAKER_USER_ROOT"))
		if (path = wglobpath(gspath)) {
			wwarning(_("variable WMAKER_USER_ROOT is deprecated; use GNUSTEP_USER_DEFAULTS_DIR or XDG_CONFIG_HOME"));
puts("WMAKER_USER_ROOT");
puts(path);
			return path;
		}

	if (!path) {
		path = wglobpath(GSUSER_DIR);
puts("GSUSER_DIR");
puts(path);
	}

	return path;
}

const char *wuserdatapath()
{
	static char *path = NULL;
	char *datapath = NULL;

	if (path)
		/* Value have been already computed, re-use it */
		return path;

	if (datapath = GETENV("GNUSTEP_USER_LIBRARY")) {
		if (!(path = wglobpath(datapath))) {
			wwarning(_("variable GNUSTEP_USER_LIBRARY defined with invalid path, not used"));
		}
puts("GNUSTEP_USER_LIBRARY");
puts(path);
	}

	if (datapath = GETENV("XDG_DATA_HOME")) {
		if (!(path = wglobpath(datapath))) {
			wwarning(_("variable XDG_DATA_HOME defined defined with invalid path, not used"));
		}
puts("XDG_DATA_HOME");
puts(path);
	}

	if (!path) {
		path = wstrappend(wstrdup(wusergnusteppath()), USERDATA_SUBDIR);
puts("Falling back to wusergnusteppath() as no configuration provided.");
puts(path);
	}

	return path;
}

char *wdefaultspathfordomain(const char *domain)
{
	char *path;
	static char *udefpath = NULL;

	if (!udefpath) {
		if (path = GETENV("GNUSTEP_USER_DEFAULTS_DIR")) {
puts("GNUSTEP_USER_DEFAULTS_DIR");
			path = wstrappend(wglobpath("~"), path);
			if (udefpath = wglobpath(path))
				wfree(path);
			else
				wwarning(_("variable GNUSTEP_USER_DEFAULTS_DIR defined with invalid path, not used"));
puts(udefpath);
		}
	} 

	if (!udefpath) {
		if (path = GETENV("XDG_CONFIG_HOME")) {
puts("XDG_CONFIG_HOME");
			if (path = wglobpath(path))
				udefpath = path;
			else
				wwarning(_("variable XDG_CONFIG_HOME defined with invalid path, not used"));
puts(udefpath);
		}
	}

	if (!udefpath) {
puts(DEFAULTS_SUBDIR);
		udefpath = wstrappend(wstrdup(wusergnusteppath()), DEFAULTS_SUBDIR);
puts(udefpath);
	}

//puts(udefpath);
	path = wstrconcat(udefpath, domain);
puts(path);
	return path;
}

/* XXX: doesn't quite belong to *user*defaults.c */
char *wglobaldefaultspathfordomain(const char *domain)
{
	char *t = NULL;
	int result;

	result = asprintf(&t, "%s/%s", PKGCONFDIR, domain);

	return t;
}

void w_save_defaults_changes(void)
{
	if (WMApplication.applicationName == NULL) {
		/*
		 * This means that the user has properly exited by calling the
		 * function 'WMReleaseApplication' (which has already called us)
		 * but we're being called again by the fallback 'atexit' method
		 * (the legacy way of saving changes on exit which is kept for
		 * application that would forget to call 'WMReleaseApplication')
		 */
		return;
	}

	wwarning(_("Failed to call WMReleaseApplication (%s)"), WMApplication.applicationName);
	/* save the user defaults databases */
	synchronizeUserDefaults(NULL);
}

/* set to save changes in defaults when program is exited */
static void registerSaveOnExit(void)
{
	static Bool registeredSaveOnExit = False;

	if (!registeredSaveOnExit) {
		atexit(w_save_defaults_changes);
		registeredSaveOnExit = True;
	}
}

static void synchronizeUserDefaults(void *foo)
{
	UserDefaults *database = sharedUserDefaults;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	while (database) {
		if (!database->dontSync)
			WMSynchronizeUserDefaults(database);
		database = database->next;
	}
}

#ifndef HAVE_INOTIFY
static void addSynchronizeTimerHandler(void)
{
	static Bool initialized = False;

	if (!initialized) {
		WMAddPersistentTimerHandler(UD_SYNC_INTERVAL,
		    synchronizeUserDefaults, NULL);
		initialized = True;
	}
}
#endif

void WMEnableUDPeriodicSynchronization(WMUserDefaults * database, Bool enable)
{
	database->dontSync = !enable;
}

void WMSynchronizeUserDefaults(WMUserDefaults * database)
{
	Bool fileIsNewer = False, release = False, notify = False;
	WMPropList *plF, *key;
	char *path;
	struct stat stbuf;

	if (!database->path) {
		path = wdefaultspathfordomain(WMGetApplicationName());
		release = True;
	} else {
		path = database->path;
	}

	if (stat(path, &stbuf) >= 0 && stbuf.st_mtime > database->timestamp)
		fileIsNewer = True;

	if (database->appDomain && (database->dirty || fileIsNewer)) {
		if (database->dirty && fileIsNewer) {
			plF = WMReadPropListFromFile(path);
			if (plF) {
				plF = WMMergePLDictionaries(plF, database->appDomain, False);
				WMReleasePropList(database->appDomain);
				database->appDomain = plF;
				key = database->searchList[0];
				WMPutInPLDictionary(database->defaults, key, plF);
				notify = True;
			} else {
				/* something happened with the file. just overwrite it */
				wwarning(_("cannot read domain from file '%s' when syncing"), path);
				WMWritePropListToFile(database->appDomain, path);
			}
		} else if (database->dirty) {
			WMWritePropListToFile(database->appDomain, path);
		} else if (fileIsNewer) {
			plF = WMReadPropListFromFile(path);
			if (plF) {
				WMReleasePropList(database->appDomain);
				database->appDomain = plF;
				key = database->searchList[0];
				WMPutInPLDictionary(database->defaults, key, plF);
				notify = True;
			} else {
				/* something happened with the file. just overwrite it */
				wwarning(_("cannot read domain from file '%s' when syncing"), path);
				WMWritePropListToFile(database->appDomain, path);
			}
		}

		database->dirty = 0;

		if (stat(path, &stbuf) >= 0)
			database->timestamp = stbuf.st_mtime;

		if (notify) {
			WMPostNotificationName(WMUserDefaultsDidChangeNotification, database, NULL);
		}
	}

	if (release)
		wfree(path);

}

void WMSaveUserDefaults(WMUserDefaults * database)
{
	if (database->appDomain) {
		struct stat stbuf;
		char *path;
		Bool release = False;

		if (!database->path) {
			path = wdefaultspathfordomain(WMGetApplicationName());
			release = True;
		} else {
			path = database->path;
		}
		WMWritePropListToFile(database->appDomain, path);
		database->dirty = 0;
		if (stat(path, &stbuf) >= 0)
			database->timestamp = stbuf.st_mtime;
		if (release)
			wfree(path);
	}
}

WMUserDefaults *WMGetStandardUserDefaults(void)
{
	WMUserDefaults *defaults;
	WMPropList *domain;
	WMPropList *key;
	struct stat stbuf;
	char *path;
	int i;

	if (sharedUserDefaults) {
		defaults = sharedUserDefaults;
		while (defaults) {
			/* path == NULL only for StandardUserDefaults db */
			if (defaults->path == NULL)
				return defaults;
			defaults = defaults->next;
		}
	}

	/* we didn't found the database we are looking for. Create it. */
	defaults = wmalloc(sizeof(WMUserDefaults));
	defaults->defaults = WMCreatePLDictionary(NULL, NULL);
	defaults->searchList = wmalloc(sizeof(WMPropList *) * 3);

	/* application domain */
	key = WMCreatePLString(WMGetApplicationName());
	defaults->searchList[0] = key;

	/* temporary kluge. wmaker handles synchronization itself */
	if (strcmp(WMGetApplicationName(), "WindowMaker") == 0) {
		defaults->dontSync = 1;
	}

	path = wdefaultspathfordomain(WMGetFromPLString(key));

	if (stat(path, &stbuf) >= 0)
		defaults->timestamp = stbuf.st_mtime;

	domain = WMReadPropListFromFile(path);

	if (!domain)
		domain = WMCreatePLDictionary(NULL, NULL);

	wfree(path);

	defaults->appDomain = domain;

	if (domain)
		WMPutInPLDictionary(defaults->defaults, key, domain);

	/* global domain */
	key = WMCreatePLString("WMGLOBAL");
	defaults->searchList[1] = key;

	path = wdefaultspathfordomain(WMGetFromPLString(key));

	domain = WMReadPropListFromFile(path);

	wfree(path);

	if (!domain)
		domain = WMCreatePLDictionary(NULL, NULL);

	if (domain)
		WMPutInPLDictionary(defaults->defaults, key, domain);

	/* terminate list */
	defaults->searchList[2] = NULL;

	defaults->searchListArray = WMCreatePLArray(NULL, NULL);

	i = 0;
	while (defaults->searchList[i]) {
		WMAddToPLArray(defaults->searchListArray, defaults->searchList[i]);
		i++;
	}

	if (sharedUserDefaults)
		defaults->next = sharedUserDefaults;
	sharedUserDefaults = defaults;

#ifndef HAVE_INOTIFY
	addSynchronizeTimerHandler();
#endif
	registerSaveOnExit();

	return defaults;
}

WMUserDefaults *WMGetDefaultsFromPath(const char *path)
{
	WMUserDefaults *defaults;
	WMPropList *domain;
	WMPropList *key;
	struct stat stbuf;
	const char *name;
	int i;

	assert(path != NULL);

	if (sharedUserDefaults) {
		defaults = sharedUserDefaults;
		while (defaults) {
			if (defaults->path && strcmp(defaults->path, path) == 0)
				return defaults;
			defaults = defaults->next;
		}
	}

	/* we didn't found the database we are looking for. Create it. */
	defaults = wmalloc(sizeof(WMUserDefaults));
	defaults->defaults = WMCreatePLDictionary(NULL, NULL);
	defaults->searchList = wmalloc(sizeof(WMPropList *) * 2);

	/* the domain we want, go in the first position */
	name = strrchr(path, '/');
	if (!name)
		name = path;
	else
		name++;

	key = WMCreatePLString(name);
	defaults->searchList[0] = key;

	if (stat(path, &stbuf) >= 0)
		defaults->timestamp = stbuf.st_mtime;

	domain = WMReadPropListFromFile(path);

	if (!domain)
		domain = WMCreatePLDictionary(NULL, NULL);

	defaults->path = wstrdup(path);

	defaults->appDomain = domain;

	if (domain)
		WMPutInPLDictionary(defaults->defaults, key, domain);

	/* terminate list */
	defaults->searchList[1] = NULL;

	defaults->searchListArray = WMCreatePLArray(NULL, NULL);

	i = 0;
	while (defaults->searchList[i]) {
		WMAddToPLArray(defaults->searchListArray, defaults->searchList[i]);
		i++;
	}

	if (sharedUserDefaults)
		defaults->next = sharedUserDefaults;
	sharedUserDefaults = defaults;

#ifndef HAVE_INOTIFY
	addSynchronizeTimerHandler();
#endif
	registerSaveOnExit();

	return defaults;
}

/* Returns a WMPropList array with all keys in the user defaults database.
 * Free the array with WMReleasePropList() when no longer needed,
 * but do not free the elements of the array! They're just references. */
WMPropList *WMGetUDKeys(WMUserDefaults * database)
{
	return WMGetPLDictionaryKeys(database->appDomain);
}

WMPropList *WMGetUDObjectForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *domainName, *domain;
	WMPropList *object = NULL;
	WMPropList *key = WMCreatePLString(defaultName);
	int i = 0;

	while (database->searchList[i] && !object) {
		domainName = database->searchList[i];
		domain = WMGetFromPLDictionary(database->defaults, domainName);
		if (domain) {
			object = WMGetFromPLDictionary(domain, key);
		}
		i++;
	}
	WMReleasePropList(key);

	return object;
}

void WMSetUDObjectForKey(WMUserDefaults * database, WMPropList * object, const char *defaultName)
{
	WMPropList *key = WMCreatePLString(defaultName);

	database->dirty = 1;

	WMPutInPLDictionary(database->appDomain, key, object);
	WMReleasePropList(key);
}

void WMRemoveUDObjectForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *key = WMCreatePLString(defaultName);

	database->dirty = 1;

	WMRemoveFromPLDictionary(database->appDomain, key);

	WMReleasePropList(key);
}

char *WMGetUDStringForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val)
		return NULL;

	if (!WMIsPLString(val))
		return NULL;

	return WMGetFromPLString(val);
}

int WMGetUDIntegerForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;
	char *str;
	int value;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val)
		return 0;

	if (!WMIsPLString(val))
		return 0;

	str = WMGetFromPLString(val);
	if (!str)
		return 0;

	if (sscanf(str, "%i", &value) != 1)
		return 0;

	return value;
}

float WMGetUDFloatForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;
	char *str;
	float value;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val || !WMIsPLString(val))
		return 0.0;

	if (!(str = WMGetFromPLString(val)))
		return 0.0;

	if (sscanf(str, "%f", &value) != 1)
		return 0.0;

	return value;
}

Bool WMGetUDBoolForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;
	int value;
	char *str;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val)
		return False;

	if (!WMIsPLString(val))
		return False;

	str = WMGetFromPLString(val);
	if (!str)
		return False;

	if (sscanf(str, "%i", &value) == 1 && value != 0)
		return True;

	if (strcasecmp(str, "YES") == 0)
		return True;

	if (strcasecmp(str, "Y") == 0)
		return True;

	return False;
}

void WMSetUDIntegerForKey(WMUserDefaults * database, int value, const char *defaultName)
{
	WMPropList *object;
	char buffer[128];

	sprintf(buffer, "%i", value);
	object = WMCreatePLString(buffer);

	WMSetUDObjectForKey(database, object, defaultName);
	WMReleasePropList(object);
}

void WMSetUDStringForKey(WMUserDefaults * database, const char *value, const char *defaultName)
{
	WMPropList *object;

	object = WMCreatePLString(value);

	WMSetUDObjectForKey(database, object, defaultName);
	WMReleasePropList(object);
}

void WMSetUDFloatForKey(WMUserDefaults * database, float value, const char *defaultName)
{
	WMPropList *object;
	char buffer[128];

	sprintf(buffer, "%f", (double)value);
	object = WMCreatePLString(buffer);

	WMSetUDObjectForKey(database, object, defaultName);
	WMReleasePropList(object);
}

void WMSetUDBoolForKey(WMUserDefaults * database, Bool value, const char *defaultName)
{
	static WMPropList *yes = NULL, *no = NULL;

	if (!yes) {
		yes = WMCreatePLString("YES");
		no = WMCreatePLString("NO");
	}

	WMSetUDObjectForKey(database, value ? yes : no, defaultName);
}

WMPropList *WMGetUDSearchList(WMUserDefaults * database)
{
	return database->searchListArray;
}

void WMSetUDSearchList(WMUserDefaults * database, WMPropList * list)
{
	int i, c;

	if (database->searchList) {
		i = 0;
		while (database->searchList[i]) {
			WMReleasePropList(database->searchList[i]);
			i++;
		}
		wfree(database->searchList);
	}
	if (database->searchListArray) {
		WMReleasePropList(database->searchListArray);
	}

	c = WMGetPropListItemCount(list);
	database->searchList = wmalloc(sizeof(WMPropList *) * (c + 1));

	for (i = 0; i < c; i++) {
		database->searchList[i] = WMGetFromPLArray(list, i);
	}
	database->searchList[c] = NULL;

	database->searchListArray = WMDeepCopyPropList(list);
}

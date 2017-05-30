#include <stdio.h>
#include <unistd.h>
#include <windows.h>

#define MAX_OBJS 100
#define MAX_CMD_LINE 8000

void RemoveFileName(char *path)
{
	int len = strlen(path);
	for (int i = len - 1; i >= 0; i--)
	{
		if (path[i] == '\\')
		{
			path[i] = '\0';
			break;
		}
	}
}

int ExecV(const char *executable_path, char *const argv[])
{
	char cmd_line[MAX_CMD_LINE];
	strcpy(cmd_line, argv[0]);
	for (int i = 1; argv[i] != NULL; i++)
	{
		strcat(cmd_line, " ");
		strcat(cmd_line, "\"");
		strcat(cmd_line, argv[i]);
		strcat(cmd_line, "\"");
	}

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	if (!CreateProcessA(executable_path, cmd_line, 0, 0, 0, 0, 0, 0, &si, &pi))
	{
		// Could not start process;
		return -1;
	}

	// Now 'pi.hProcess' contains the process HANDLE, which you can use to wait for it like this:
	WaitForSingleObject(pi.hProcess, INFINITE);
	return 0;
}

int main(int argc, char **argv)
{
	char home_dir[MAX_PATH];
	GetModuleFileName(NULL, home_dir, MAX_PATH);
	RemoveFileName(home_dir);
	RemoveFileName(home_dir);

	char path_ld[MAX_PATH];
	char ld_Loption1[MAX_PATH];
	char ld_Loption2[MAX_PATH];
	char path_crt2[MAX_PATH];
	char path_crtbegin[MAX_PATH];
	char path_crtend[MAX_PATH];

	sprintf(path_ld, "%s\\bin\\ld.exe", home_dir);
	sprintf(ld_Loption1, "-L%s\\lib\\mingw", home_dir);
	sprintf(ld_Loption2, "-L%s\\lib\\swift\\mingw", home_dir);
	sprintf(path_crt2, "%s\\lib\\mingw\\crt2.o", home_dir);
	sprintf(path_crtbegin, "%s\\lib\\mingw\\crtbegin.o", home_dir);
	sprintf(path_crtend, "%s\\lib\\mingw\\crtend.o", home_dir);

	char *path_linkopt;
	char *path_obj[MAX_OBJS];
	int obj_count = 0;
	char *path_output;

	for (int i = 0; i < argc; i++)
	{
		if (argv[i][0] == '@')
			path_linkopt = argv[i];

		if (argv[i][0] == '-' && argv[i][1] == 'o' && i + 1 < argc)
			path_output = argv[i + 1];

		int len = strlen(argv[i]);
		if (argv[i][len - 2] == '.' && argv[i][len - 1] == 'o')
			path_obj[obj_count++] = argv[i];
	}

	char *ld_argv[MAX_OBJS];

	int t = 0;
	ld_argv[t++] = path_ld;
	ld_argv[t++] = "-m";
	ld_argv[t++] = "i386pep";
	ld_argv[t++] = "-Bdynamic";
	ld_argv[t++] = "-o";
	ld_argv[t++] = path_output;
	ld_argv[t++] = path_crt2;
	ld_argv[t++] = path_crtbegin;
	for (int i = 0; i < obj_count; i++)
		ld_argv[t++] = path_obj[i];

	ld_argv[t++] = ld_Loption1;
	ld_argv[t++] = ld_Loption2;
	ld_argv[t++] = "-lswiftSwiftOnoneSupport";
	ld_argv[t++] = "-lswiftCore";
	ld_argv[t++] = path_linkopt;
	ld_argv[t++] = "-lstdc++";
	ld_argv[t++] = "-lmingw32";
	ld_argv[t++] = "-lgcc_s";
	ld_argv[t++] = "-lgcc";
	ld_argv[t++] = "-lmoldname";
	ld_argv[t++] = "-lmingwex";
	ld_argv[t++] = "-lmsvcrt";
	ld_argv[t++] = "-ladvapi32";
	ld_argv[t++] = "-lshell32";
	ld_argv[t++] = "-luser32";
	ld_argv[t++] = "-lkernel32";

	ld_argv[t++] = "-lmingw32";
	ld_argv[t++] = "-lgcc_s";
	ld_argv[t++] = "-lgcc";
	ld_argv[t++] = "-lmoldname";
	ld_argv[t++] = "-lmingwex";
	ld_argv[t++] = "-lmsvcrt";

	ld_argv[t++] = path_crtend;

	ld_argv[t] = NULL;

	ExecV(path_ld, ld_argv);

	return 0;
}

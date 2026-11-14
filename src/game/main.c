#include "core/cli.h"
#include "game/app.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    CliOptions options;
    char error[256];
    if (!cli_parse(&options, argc, argv, error, sizeof(error))) {
        fprintf(stderr, "colony_sim: %s\n\n%s", error, cli_usage());
        return 2;
    }
    if (options.show_help) {
        fputs(cli_usage(), stdout);
        return 0;
    }

    App app;
    bool ok = app_init(&app, &options);
    if (ok) {
        app_run(&app);
    } else {
        /* Not translated on purpose: translations may be what failed to load. */
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Colony Sim",
                                 "The game failed to start. See the console output for details.",
                                 NULL);
    }
    app_shutdown(&app);
    return ok ? 0 : 1;
}

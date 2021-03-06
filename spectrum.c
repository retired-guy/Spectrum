#include <locale.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#else
#include <stdlib.h>
#endif

//#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

//#include <ctype.h>
#include <dirent.h>
#include <fftw3.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
//#include <unistd.h>

#include "debug.h"
#include "config.h"
#include "sigproc.h"

#include "input/common.h"
#include "input/alsa.h"
#include "input/fifo.h"
#include "input/pulse.h"
#include "input/shmem.h"
#include "input/sndio.h"

#include "output/fbplot.h"

#ifdef __GNUC__
// curses.h or other sources may already define
#undef GCC_UNUSED
#define GCC_UNUSED __attribute__((unused))
#else
#define GCC_UNUSED /* nothing */
#endif


bool clean_exit = false;
struct config_params p;

// general: handle signals
void sig_handler(int sig_no) {
    if (sig_no == SIGUSR1) {
        return;
    }

    if (sig_no == SIGUSR2) {
        return;
    }

    if (sig_no == SIGINT) {
        printf("CTRL-C pressed -- exiting\n");
        clean_exit = true;
        return;
    }

    signal(sig_no, SIG_DFL);
    raise(sig_no);
}

// config: reloader
void check_config_changed(char *configPath,
        rgba *plot_l_c, rgba *plot_r_c,
        rgba *ax_c, rgba *ax2_c,
        rgba *text_c, rgba *audio_c) {
    // reload if config file has been modified
    struct stat config_stat;
    static time_t last_time = 0;
    int err = stat(configPath, &config_stat);
    if (!err) {
        if (last_time != config_stat.st_mtime) {
            last_time = config_stat.st_mtime;
            debug("config file has been modified, reloading\n");
            struct error_s error;
            error.length = 0;
            if (!load_config(configPath, (void *)&p, &error)) {
                fprintf(stderr, "Error loading config. %s", error.message);
                exit(EXIT_FAILURE);
            } else {
                // config: font
                freetype_cleanup();
                freetype_init(p.text_font, p.audio_font);
                // config: plot colours
                uint32_t r, g, b;
                sscanf(p.plot_l_col, "#%02x%02x%02x", &r, &g, &b);
                plot_l_c->r = r; plot_l_c->g = g; plot_l_c->b = b;
                sscanf(p.plot_r_col, "#%02x%02x%02x", &r, &g, &b);
                plot_r_c->r = r; plot_r_c->g = g; plot_r_c->b = b;
                sscanf(p.ax_col, "#%02x%02x%02x", &r, &g, &b);
                ax_c->r = r; ax_c->g = g; ax_c->b = b;
                sscanf(p.ax_2_col, "#%02x%02x%02x", &r, &g, &b);
                ax2_c->r = r; ax2_c->g = g; ax2_c->b = b;
                sscanf(p.text_col, "#%02x%02x%02x", &r, &g, &b);
                text_c->r = r; text_c->g = g; text_c->b = b;
                sscanf(p.audio_col, "#%02x%02x%02x", &r, &g, &b);
                audio_c->r = r; audio_c->g = g; audio_c->b = b;
            }
        }
    }
}

int main(int argc, char **argv) {

    int exit_condition = EXIT_SUCCESS;

    char *usage = "\n\
Usage : " PACKAGE " [options]\n\
Visualize audio input on the framebuffer. \n\
\n\
Options:\n\
	-p          path to config file\n\
	-v          print version\n\
\n\
All options are specified in config file, see in '/home/username/.config/spectrum/' \n";

    // general: handle Ctrl+C and other signals
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = &sig_handler;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    // general: handle command-line arguments
    int c;
    char configPath[PATH_MAX];
    configPath[0] = '\0';
    while ((c = getopt(argc, argv, "p:vh")) != -1) {
        switch (c) {
        case 'p': // argument: fifo path
            snprintf(configPath, sizeof(configPath), "%s", optarg);
            break;
        case 'h': // argument: print usage
            printf("%s", usage);
            return EXIT_FAILURE;
        case '?': // argument: print usage
            printf("%s", usage);
            return EXIT_FAILURE;
        case 'v': // argument: print version
            printf(PACKAGE " " VERSION "\n");
            return EXIT_SUCCESS;
        default: // argument: no arguments; exit
            abort();
        }
    }

    // config: load the config file
    debug("loading config\n");
    struct error_s error;
    error.length = 0;
    if (!load_config(configPath, &p, &error)) {
        fprintf(stderr, "Error loading config. %s", error.message);
        exit(EXIT_FAILURE);
    }

    // config: font
    freetype_init(p.text_font, p.audio_font);

    // config: plot colours
    uint32_t r, g, b, a=0;
    sscanf(p.plot_l_col, "#%02x%02x%02x", &r, &g, &b);
    rgba plot_l_c   = {r, g, b, a};
    sscanf(p.plot_r_col, "#%02x%02x%02x", &r, &g, &b);
    rgba plot_r_c   = {r, g, b, a};
    sscanf(p.ax_col, "#%02x%02x%02x", &r, &g, &b);
    rgba ax_c   = {r, g, b, a};
    sscanf(p.ax_2_col, "#%02x%02x%02x", &r, &g, &b);
    rgba ax2_c   = {r, g, b, a};
    sscanf(p.text_col, "#%02x%02x%02x", &r, &g, &b);
    rgba text_c   = {r, g, b, a};
    sscanf(p.audio_col, "#%02x%02x%02x", &r, &g, &b);
    rgba audio_c   = {r, g, b, a};
    rgba bar_c = {0,255,0,a};

    /*** set up framebuffer display ***/

    // left channel axes
    axes ax_l;
    ax_l.screen_x = 0;
    ax_l.screen_y = 0;
    ax_l.screen_w = FRAMEBUFFER_WIDTH - 1;
    ax_l.screen_h = FRAMEBUFFER_HEIGHT;
    ax_l.x_min = log10(LOWER_CUTOFF_FREQ);
    ax_l.x_max = log10(UPPER_CUTOFF_FREQ);
    ax_l.y_min = -1000;    // dB
    ax_l.y_max = 500;

    // right channel axes are an offset copy of the left
    axes ax_r = ax_l;
    ax_r.screen_x = 1; // offset so l/r channels alternate pixels

    int number_of_bars = 30; //ax_l.screen_w / 2;

    // framebuffer plotting init
    fb_setup();
    fb_clear();

    buffer buffer_final;
    bf_init(&buffer_final);
    bf_clear(buffer_final);
    buffer buffer_clock;
    bf_init(&buffer_clock);

    /*** set up audio processing ***/

    struct audio_data audio;
    memset(&audio, 0, sizeof(audio));

    // input: init
    audio.source = malloc(1 + strlen(p.audio_source));
    strcpy(audio.source, p.audio_source);

    audio.format = -1;
    audio.rate = 0;
    audio.FFTbufferSize = 8192;
    audio.terminate = 0;
    audio.channels = 2;
    audio.index = 0;
    audio.running = 1;

    // allocate fft memory
    audio.in_r = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
    audio.in_l = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
    memset(audio.in_r, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(double));
    memset(audio.in_l, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(double));

    audio.windowed_r = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
    audio.windowed_l = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));

    audio.out_l = fftw_alloc_complex(2 * (audio.FFTbufferSize / 2 + 1));
    audio.out_r = fftw_alloc_complex(2 * (audio.FFTbufferSize / 2 + 1));
    memset(audio.out_l, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(fftw_complex));
    memset(audio.out_r, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(fftw_complex));

    fftw_plan p_l, p_r;
    p_l = fftw_plan_dft_r2c_1d(audio.FFTbufferSize, audio.windowed_l, audio.out_l, FFTW_MEASURE);
    p_r = fftw_plan_dft_r2c_1d(audio.FFTbufferSize, audio.windowed_r, audio.out_r, FFTW_MEASURE);

    debug("got buffer size: %d, %d, %d", audio.FFTbufferSize);

    reset_output_buffers(&audio);

    /*** set up audio input ***/

    debug("starting audio thread\n");
    pthread_t p_thread;
    int thr_id GCC_UNUSED;
    int thr_idLMS GCC_UNUSED;

    int sourceIsAuto = 1;
    // Input is shared memory from squeezelite on this box
    thr_id = pthread_create(&p_thread, NULL, input_shmem, (void *)&audio);

    int n = 0;

    while (audio.rate == 0) {
        struct timespec req = {.tv_sec = 0, .tv_nsec = 1e8};
        nanosleep(&req, NULL);
        n++;
        if (n > 2000) {
            fb_cleanup();
            fprintf(stderr, "could not get rate and/or format, problems with audio thread? "
                            "quiting...\n");
            exit(EXIT_FAILURE);
        }
    }
    debug("got format: %d and rate %d\n", audio.format, audio.rate);

    /*** main loop ***/

    // loop-scope variables
    time_t now;
    time_t n1;
    struct tm *info;
    clock_t fps_timer = 10;
    clock_t last_fps_timer = 11;
    double fps = 30;
    char textstr[80];
    char timestr[80];
    int length;
    double peak_dB = -10.0;
    double peak_l = 0, peak_r = 0;
    double ppm_l = -60, ppm_r = -60;
    uint32_t l_pos = 0;
    uint32_t r_pos = 0;

    time(&n1);

    while (!clean_exit) {

        time(&now);
        last_fps_timer = fps_timer;
        fps_timer = clock();
        double dt = (double)(fps_timer - last_fps_timer) / CLOCKS_PER_SEC;

        // if config file is modified, reloads every 10s

        if ((now % 10) == 0) {
          if ( now - n1 > 1) {
            check_config_changed(configPath,
                    &plot_l_c, &plot_r_c,
                    &ax_c, &ax2_c,
                    &text_c, &audio_c);

             time(&n1);
          } 
        }

#ifdef NDEBUG
        // framebuffer vis

        // wait for screen to be ready
        fb_vsync();
        bf_blit(buffer_final);

        if (!audio.running) {
            // if audio is paused wait and continue
            // show a clock, screensaver or something
            bf_clear(buffer_clock);
            time(&now);
            length = strftime(textstr, sizeof(textstr), "%H:%M", localtime(&now));
            bf_text(buffer_clock, textstr, length, 64, true, 0, 200, 1, text_c);
            length = strftime(textstr, sizeof(textstr), "%a, %d %B %Y", localtime(&now));
            bf_text(buffer_clock, textstr, length, 14, true, 0, 80, 1, text_c);
            bf_blend(buffer_final, buffer_clock, 0.98);
            //bf_blit(buffer_clock);

            // wait, then check if running again.
            struct timespec sleep_mode_timer = {.tv_sec = 0, .tv_nsec = 3e8};
            nanosleep(&sleep_mode_timer, NULL);
            continue;

        } else if (!strcmp("fft", p.vis)) {

            // window, execute FFT
            window(&audio, HANN);
            //window(&audio, BLAC);
            fftw_execute(p_l);
            fftw_execute(p_r);

            // integrate power
            int *bins_left = make_bins(&audio, number_of_bars, LEFT_CHANNEL); 
            int *bins_right = make_bins(&audio, number_of_bars, RIGHT_CHANNEL); 

            // FFT plotter to framebuffer
            // set plotting axes
            for (int n = 0; n < number_of_bars; n++) {
                double dB = 10 * log10(fmax(bins_left[n], bins_right[n]));
                peak_dB = fmax(dB, peak_dB);
                ax_l.y_max = peak_dB;
                ax_l.y_min = peak_dB + p.noise_floor;
                ax_r.y_max = peak_dB;
                ax_r.y_min = peak_dB + p.noise_floor;
            }

            // monstercat smoothing
            int z,m_y,de;
            double monstercat = 5;

            for (z = 0; z < number_of_bars; z++) { 
              for (m_y = z - 1; m_y >= 0; m_y--) {
                de = z - m_y;
                bins_left[m_y] = fmax(bins_left[z] / pow(monstercat,de), bins_left[m_y]);
                bins_right[m_y] = fmax(bins_right[z] / pow(monstercat,de), bins_right[m_y]);
              }

              for (m_y = z + 1; m_y < number_of_bars; m_y++) {
                de = m_y - z;
                bins_left[m_y] = fmax(bins_left[z] / pow(monstercat,de), bins_left[m_y]);
                bins_right[m_y] = fmax(bins_right[z] / pow(monstercat,de), bins_right[m_y]);
              }
            }

//            bf_shade(buffer_final, p.alpha);
            bf_clear(buffer_final);
            // plot spectrum
            //bf_plot_bars(buffer_final, ax_l, bins_right, number_of_bars, plot_l_c);
            //bf_plot_bars(buffer_final, ax_r, bins_left, number_of_bars, plot_r_c);
            bf_plot_bars(buffer_final, ax_l, bins_right, number_of_bars, bar_c);
            bf_plot_bars(buffer_final, ax_r, bins_left, number_of_bars, bar_c);
            bf_plot_axes(buffer_final, ax_l, ax_c, ax_c);

            for (int n = 0; n < audio.FFTbufferSize; n++) {
                ax_l.y_max = fmax(ax_l.y_max, audio.in_l[n]/100);
            }

            // PPM
            // peak_l and peak_r are averaged over last 5ms
            peak_l = 0;
            peak_r = 0;
            bool clip = false;
            int num_samples = (int)(5.0 * (double)audio.rate / 1000.0);
            for (int n = 0; n < num_samples; n++) {
                int i = (n + audio.index) % audio.FFTbufferSize;
                peak_l += fabs(audio.in_l[i]);
                peak_r += fabs(audio.in_r[i]);
                // clip if very very close to max possible value
                clip = clip || (fmax(fabs((double)audio.in_l[i]), fabs((double)audio.in_r[i])) >= ((2 << 15) - 2));
            }
            peak_l /= num_samples;
            peak_r /= num_samples;
            // Audio came from a signed 16-bit int, so clipping occurs at < 90.3dB.
            // The scale is defined with 0dB relative to 10dB headroom.
            // As such, subtract absolute 81dB so that instantaneous +10dB on
            // the scale is where clipping occurs.
            double min_dB = -60;
            // Physical dynamics of ppm meters:
            // the pole at -1.3545 corresponds to 20dB decay / 1.7s
            // as per Type I IEC 60268-10 (DIN PPM) spec.
            // ppm_l, ppm_r are the meter readings in dB.
            ppm_l = exp(-1.3545 * dt) * ppm_l + fmax(20 * log10(peak_l) - 80.3, min_dB) * dt;
            ppm_r = exp(-1.3545 * dt) * ppm_r + fmax(20 * log10(peak_r) - 80.3, min_dB) * dt;


            l_pos = abs((ax_l.screen_w-160)-abs(ppm_l)*15)+60;
            r_pos = abs((ax_l.screen_w-160)-abs(ppm_r)*15)+60;
            bf_text(buffer_final,"L",1,8,false,20,80,0,audio_c);
            bf_draw_line(buffer_final, 60, 85, l_pos, 85, bar_c);
            bf_text(buffer_final,"R",1,8,false,20,40,0,audio_c);
            bf_draw_line(buffer_final, 60, 45, r_pos, 45, bar_c);

            if( (now % 1) == 0 ) {
              info = localtime(&now);
              strftime(timestr,80,"%a,  %b  %d  %I:%M %p", info);
              bf_text(buffer_final,timestr,strlen(timestr),9,false,60,10,0,audio_c);
            }
          
            sprintf(textstr, "%+03.1f  ", ppm_r);
            bf_text(buffer_final, textstr, 5, 8, false, ax_l.screen_w - 60, 80, 0, audio_c);
            sprintf(textstr, "%+03.1f  ", ppm_l);
            bf_text(buffer_final, textstr, 5, 8, false, ax_l.screen_w - 60, 40, 0, audio_c);
   
            sprintf(textstr, "%4.1fkHz", (double)audio.rate / 1000);
            bf_text(buffer_final, textstr, 7, 9, false, 710, 10, 0, audio_c);

	    //bf_blit(buffer_final);
            //bf_clear(buffer_final);
            
        } else if (!strcmp("pcm", p.vis)) {
            // waveform plotter to framebuffer
            // set plotting axes
            for (int n = 0; n < audio.FFTbufferSize; n++) {
                //int i = (n + audio.index) % audio.FFTbufferSize;
                ax_l.y_max = fmax(ax_l.y_max, audio.in_l[n]);
                ax_l.y_min = fmin(ax_l.y_min, audio.in_l[n]);
                ax_l.y_max = fmax(ax_l.y_max, audio.in_r[n]);
                ax_l.y_min = fmin(ax_l.y_min, audio.in_r[n]);
                ax_r.y_max = ax_l.y_max;
                ax_r.y_min = ax_l.y_min;
            }

            // plot waveform
            bf_clear(buffer_final);
            bf_plot_line(buffer_final, ax_l, audio.in_l, audio.FFTbufferSize, plot_l_c);
            bf_plot_line(buffer_final, ax_r, audio.in_r, audio.FFTbufferSize, plot_r_c);

        } else {
            // PPM
            // peak_l and peak_r are averaged over last 5ms
            peak_l = 0;
            peak_r = 0;
            bool clip = false;
            int num_samples = (int)(5.0 * (double)audio.rate / 1000.0);
            for (int n = 0; n < num_samples; n++) {
                int i = (n + audio.index) % audio.FFTbufferSize;
                peak_l += fabs(audio.in_l[i]);
                peak_r += fabs(audio.in_r[i]);
                // clip if very very close to max possible value
                clip = clip || (fmax(fabs((double)audio.in_l[i]), fabs((double)audio.in_r[i])) >= ((2 << 15) - 2));
            }
            peak_l /= num_samples;
            peak_r /= num_samples;
            // Audio came from a signed 16-bit int, so clipping occurs at < 90.3dB.
            // The scale is defined with 0dB relative to 10dB headroom.
            // As such, subtract absolute 81dB so that instantaneous +10dB on
            // the scale is where clipping occurs.
            //double max_angle = 45;
            //double min_angle = 135;
            double max_angle = 60;
            double min_angle = -60;
            double max_angle_r = 120;
            double min_angle_r = 240;

            double max_dB = 5;
            double min_dB = -50;
            // Linear relation between dB and angle: theta = m*dB + c.
            double m = (max_angle - min_angle) / (max_dB - min_dB);
            double c = max_angle - m * max_dB;

            double m_r = (max_angle_r - min_angle_r) / (max_dB - min_dB);
            double c_r = max_angle_r - m_r * max_dB;

            // Physical dynamics of ppm meters:
            // the pole at -1.3545 corresponds to 20dB decay / 1.7s
            // as per Type I IEC 60268-10 (DIN PPM) spec.
            // ppm_l, ppm_r are the meter readings in dB.
            ppm_l = exp(-1.3545 * dt) * ppm_l + fmax(20 * log10(peak_l) - 80.3, min_dB) * dt;
            ppm_r = exp(-1.3545 * dt) * ppm_r + fmax(20 * log10(peak_r) - 80.3, min_dB) * dt;
            double angle_l = ppm_l * m + c;
            //double angle_r = ppm_r * m + c;
            double angle_r = ppm_r * m_r + c_r;
            // Draw left and right needles on one dial.
            //int r = 320;                        // needle radius
            int r = 180;
//            int x0 = (int)buffer_final.w / 2;   // needle origin (x)
            int x0 = 80;
            int y0 = (int)buffer_final.h / 2;     // needle origin (y)
            int xr0 = (int)buffer_final.w - 80;
            int yr0 = y0;

            // render the dial to the buffer
            bf_clear(buffer_final);
            bf_text(buffer_final, "DIN PPM", 7, 10, false, ax_l.screen_w/2 - 40, ax_l.screen_y + ax_l.screen_h - 80, 0, audio_c);
            // dB scale markings
            for (double dB = min_dB; dB < 0; dB += 5) {
                bf_draw_ray(buffer_final, x0, y0, r+3, r+10, dB * m + c, 3, ax_c);
            }
            for (double dB = min_dB; dB < 0; dB += 10) {
                bf_draw_ray(buffer_final, x0, y0, r+3, r+22, dB * m + c, 3, ax_c);
            }
            

            for (double dB = min_dB; dB < 0; dB += 5) {
                bf_draw_ray(buffer_final, xr0, yr0, r+3, r+10, dB * m_r + c_r, 3, ax_c);
            }
            for (double dB = min_dB; dB < 0; dB += 10) {
                bf_draw_ray(buffer_final, xr0, yr0, r+3, r+22, dB * m_r + c_r, 3, ax_c);
            }


            // scale labels
            int x, y;
            bf_ray_xy(x0, y0, r + 30, -50 * m + c, &x, &y);
            bf_text(buffer_final, "-50", 3, 8, false, x-20, y-20, 0, audio_c);
            bf_ray_xy(x0, y0, r + 30, c, &x, &y);
            bf_text(buffer_final, "0", 1, 8, false, x + 3, y + 8, 0, audio_c);
            bf_ray_xy(x0, y0, r + 30, 5 * m + c, &x, &y);
            bf_text(buffer_final, "+5", 2, 8, false, x-10, y, 0, ax2_c);
            // dB excess
            for (double dB = 0; dB <= max_dB; dB += 5) {
                bf_draw_ray(buffer_final, x0, y0, r+10, r+22, dB * m + c, 3, ax2_c);
            }


            bf_ray_xy(xr0, yr0, r + 30, -50 * m_r + c_r, &x, &y);
            bf_text(buffer_final, "-50", 3, 8, false, x-20, y-20, 0, audio_c);
            bf_ray_xy(xr0, yr0, r + 30, c_r, &x, &y);
            bf_text(buffer_final, "0", 1, 8, false, x + 3, y + 8, 0, audio_c);
            bf_ray_xy(xr0, yr0, r + 30, 5 * m_r + c_r, &x, &y);
            bf_text(buffer_final, "+5", 2, 8, false, x-10, y, 0, ax2_c);
            // dB excess
            for (double dB = 0; dB <= max_dB; dB += 5) {
                bf_draw_ray(buffer_final, xr0, yr0, r+10, r+22, dB * m_r + c_r, 3, ax2_c);
            }

            // main dial
            bf_draw_arc(buffer_final, x0, y0, r, min_dB * m + c, max_dB * m + c, 2, ax_c);
            bf_draw_arc(buffer_final, xr0, yr0, r, min_dB * m_r + c_r, max_dB * m_r + c_r, 2, ax_c);

            // dial excess; glow if hit
            if (ppm_l >= 0 || ppm_r >= 0 || clip) {
                rgba excess_c = ax2_c;
                excess_c.r = 255;
                bf_draw_arc(buffer_final, x0, y0, r+10, c, max_dB * m + c, 6, excess_c);
            } else {
                bf_draw_arc(buffer_final, x0, y0, r+10, c, max_dB * m + c, 5, ax2_c);
            }
            // readings
            bf_draw_ray(buffer_final, x0, y0, r - 160, r + 20, angle_l, 5, plot_l_c);
            //bf_draw_ray(buffer_final, x0, y0, r - 100, r + 20, angle_r, 5, plot_r_c);
            bf_draw_ray(buffer_final, xr0, yr0, r - 160, r + 20, angle_r, 5, plot_r_c);
            sprintf(textstr, "%+03.0fdB", ppm_l);
            bf_text(buffer_final, textstr, 5, 8, false, ax_l.screen_x + 10, y0, 0, audio_c);
            sprintf(textstr, "%+03.0fdB", ppm_r);
            bf_text(buffer_final, textstr, 5, 8, false, ax_l.screen_w - 60, y0, 0, audio_c);
            bf_text(buffer_final, "dB", 2, 16, true, 0, y0, 0, audio_c);

            // sampling rate
            sprintf(textstr, "%4.1fkHz", (double)audio.rate / 1000);
            bf_text(buffer_final, textstr, 7, 10, false, ax_l.screen_w / 2 - 40, 80, 0, audio_c);
        }
        

        /* debugging info for the display
            sprintf(textstr, "%+7.2f peak_dB", peak_dB);
            bf_text(buffer_final, textstr, 15, 8, false, ax_l.screen_x, ax_l.screen_y + ax_l.screen_h - 80, 0, audio_c);
            sprintf(textstr, "%+7.2f noise_floor", p.noise_floor);
            bf_text(buffer_final, textstr, 19, 8, false, ax_l.screen_x, ax_l.screen_y + ax_l.screen_h - 110, 0, audio_c);
        end debugging info */

        // stuff common to all vis follows
        fps = fps * 0.995 + (1.0 - 0.995) / dt;
        sprintf(textstr, "%2.0f fps", fps);
        
        //bf_text(buffer_final, textstr, 6, 8, false, ax_l.screen_x + ax_l.screen_w - 60, ax_l.screen_y + ax_l.screen_h -80, 0, audio_c);



#endif
        // check if audio thread has exited unexpectedly
        if (audio.terminate == 1) {
            fprintf(stderr, "Audio thread exited unexpectedly. %s\n", audio.error_message);
            break;
            exit_condition = EXIT_FAILURE;
        }

    }

    /*** exit ***/

    // free screen buffers
    bf_free_pixels(&buffer_final);
    bf_free_pixels(&buffer_clock);
    fb_cleanup();

    // tell input thread to terminate
    audio.terminate = 1;
    pthread_join(p_thread, NULL);

    if (sourceIsAuto)
        free(audio.source);

    // free fft working space
    fftw_free(audio.in_r);
    fftw_free(audio.in_l);
    fftw_free(audio.windowed_r);
    fftw_free(audio.windowed_l);
    fftw_free(audio.out_r);
    fftw_free(audio.out_l);
    fftw_destroy_plan(p_l);
    fftw_destroy_plan(p_r);
    fftw_cleanup();

    freetype_cleanup();

    return exit_condition;
}

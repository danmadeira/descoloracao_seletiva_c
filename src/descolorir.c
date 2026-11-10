/**
 * @file descolorir.c
 * @author Daniel Madeira
 * @brief Programa de Descoloração Seletiva em C (PNG, TIFF, JPEG, GIF, WebP, BMP e PPM/PGM).
 * @version 1.0
 * @date 2026-11-09
 *
 * @details Suporta leitura e escrita no mesmo formato ou conversão entre formatos,
 *          mantendo coloridos apenas os pixels contidos nos intervalos definidos
 *          no espaço de cores HSL (Matiz, Saturação e Luminosidade) e convertendo
 *          os demais para escala de cinza segundo a recomendação ITU-R BT.709.
 *
 * @note
 * Compilação:
 * @code
 *   gcc src/descolorir.c -o src/descolorir -lpng -ltiff -ljpeg -lgif -lwebp -lm
 * @endcode
 *
 * Uso:
 * @code
 *   ./descolorir <arquivo_entrada> -o <arquivo_saida> [-h <inicio> [<fim>]] [-s <inicio> [<fim>]] [-l <inicio> [<fim>]] [opções]
 * @endcode
 *
 * @copyright Copyright (c) 2026 Daniel Madeira. Todos os direitos reservados.
 * @license https://opensource.org/license/GPL-3.0 GNU Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <png.h>
#include <tiffio.h>
#include <jpeglib.h>
#include <gif_lib.h>
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/types.h>

/**
 * @brief Quantiza buffers RGB de 24 bits para uma paleta indexada de até 256 cores (função da libgif).
 *
 * @param[in]     Width          Largura da imagem em pixels.
 * @param[in]     Height         Altura da imagem em pixels.
 * @param[in,out] ColorMapSize   Na entrada, número máximo de cores desejado; na saída, tamanho efetivo da paleta.
 * @param[in]     RedInput       Buffer contendo o canal vermelho (R) de cada pixel.
 * @param[in]     GreenInput     Buffer contendo o canal verde (G) de cada pixel.
 * @param[in]     BlueInput      Buffer contendo o canal azul (B) de cada pixel.
 * @param[out]    OutputBuffer   Buffer de saída com os índices da paleta para cada pixel.
 * @param[out]    OutputColorMap Vetor de cores da paleta gerada.
 * @return GIF_OK em caso de sucesso ou GIF_ERROR em caso de falha.
 */
int GifQuantizeBuffer(unsigned int Width, unsigned int Height,
                      int *ColorMapSize,
                      const GifByteType *RedInput,
                      const GifByteType *GreenInput,
                      const GifByteType *BlueInput,
                      GifByteType *OutputBuffer,
                      GifColorType *OutputColorMap);

/**
 * @enum FormatoImagem
 * @brief Formatos de arquivo de imagem suportados pelo programa.
 */
typedef enum {
    FORMATO_DESCONHECIDO = 0, /**< Formato não identificado ou não suportado. */
    FORMATO_PNG,              /**< Portable Network Graphics (.png). */
    FORMATO_TIFF,             /**< Tagged Image File Format (.tif, .tiff). */
    FORMATO_JPEG,             /**< Joint Photographic Experts Group (.jpg, .jpeg). */
    FORMATO_GIF,              /**< Graphics Interchange Format (.gif). */
    FORMATO_WEBP,             /**< Formato de imagem WebP (.webp). */
    FORMATO_BMP,              /**< Windows Bitmap (.bmp). */
    FORMATO_PPM               /**< Netpbm Portable Pixmap / Graymap / Anymap (.ppm, .pgm, .pnm). */
} FormatoImagem;

/**
 * @struct HSL
 * @brief Representa uma cor no espaço de cores HSL (Hue, Saturation, Lightness).
 */
typedef struct {
    int hue;        /**< Matiz em graus (0 a 360). */
    int saturation; /**< Saturação em porcentagem (0 a 100). */
    int lightness;  /**< Luminosidade em porcentagem (0 a 100). */
} HSL;

/**
 * @struct IntervaloHSL
 * @brief Define os limites inferiores e superiores de seleção no espaço HSL.
 */
typedef struct {
    int hi; /**< Matiz inferior (0..360). */
    int hs; /**< Matiz superior (0..360). */
    int si; /**< Saturação inferior (0..100). */
    int ss; /**< Saturação superior (0..100). */
    int li; /**< Luminosidade inferior (0..100). */
    int ls; /**< Luminosidade superior (0..100). */
} IntervaloHSL;

/**
 * @struct Imagem
 * @brief Estrutura em memória para armazenar uma imagem decodificada em RGB ou RGBA (8 bits por canal).
 */
typedef struct {
    uint32_t largura; /**< Largura da imagem em pixels. */
    uint32_t altura;  /**< Altura da imagem em pixels. */
    int canais;       /**< Número de canais por pixel: 3 (RGB) ou 4 (RGBA). */
    uint8_t *pixels;  /**< Buffer contínuo de largura * altura * canais bytes. */
} Imagem;

/**
 * @brief Exibe uma mensagem de erro formatada na saída de erro padrão (`stderr`) e encerra o programa.
 *
 * @param[in] fmt String de formatação no estilo `printf`.
 * @param[in] ... Argumentos variáveis correspondentes à string de formatação.
 */
static void erro_fatal(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Erro: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(EXIT_FAILURE);
}

/**
 * @brief Libera a memória alocada para o buffer de pixels de uma estrutura `Imagem`.
 *
 * @param[in,out] img Ponteiro para a estrutura `Imagem` cujo buffer será liberado.
 */
static void liberar_imagem(Imagem *img)
{
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
}

/**
 * @brief Converte uma cor do espaço RGB (8 bits por canal) para o espaço HSL.
 *
 * @param[in] r Componente vermelha (Red), de 0 a 255.
 * @param[in] g Componente verde (Green), de 0 a 255.
 * @param[in] b Componente azul (Blue), de 0 a 255.
 * @return Estrutura `HSL` contendo matiz (0..360), saturação (0..100) e luminosidade (0..100).
 */
static HSL rgb_para_hsl(uint8_t r, uint8_t g, uint8_t b)
{
    double red = r / 255.0;
    double green = g / 255.0;
    double blue = b / 255.0;

    double max = red;
    if (green > max) max = green;
    if (blue > max) max = blue;

    double min = red;
    if (green < min) min = green;
    if (blue < min) min = blue;

    double lightness = (max + min) / 2.0;
    double saturation = 0.0;
    double hue = 0.0;

    if (max == min) {
        saturation = 0.0;
        hue = 0.0;
    } else {
        if (lightness < 0.5) {
            saturation = (max - min) / (max + min);
        } else {
            saturation = (max - min) / (2.0 - max - min);
        }

        if (red >= green && red >= blue) {
            hue = ((green - blue) / (max - min)) * 60.0;
        } else if (green >= red && green >= blue) {
            hue = (2.0 + (blue - red) / (max - min)) * 60.0;
        } else {
            hue = (4.0 + (red - green) / (max - min)) * 60.0;
        }

        if (hue < 0.0) {
            hue += 360.0;
        }
    }

    HSL hsl;
    hsl.hue = (int)round(hue);
    hsl.saturation = (int)round(saturation * 100.0);
    hsl.lightness = (int)round(lightness * 100.0);
    return hsl;
}

/**
 * @brief Converte uma cor do espaço HSL para o espaço RGB (8 bits por canal).
 *
 * @param[in]  h Matiz (Hue) no intervalo [0.0, 360.0].
 * @param[in]  s Saturação (Saturation) no intervalo [0.0, 100.0].
 * @param[in]  l Luminosidade (Lightness) no intervalo [0.0, 100.0].
 * @param[out] r Ponteiro onde será armazenada a componente vermelha (0..255).
 * @param[out] g Ponteiro onde será armazenada a componente verde (0..255).
 * @param[out] b Ponteiro onde será armazenada a componente azul (0..255).
 */
static void hsl_para_rgb(double h, double s, double l, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double hue = h / 360.0;
    double sat = s / 100.0;
    double lig = l / 100.0;
    double red, green, blue;

    if (sat <= 0.0) {
        red = green = blue = lig;
    } else {
        double sl1 = (lig < 0.5) ? (lig * (1.0 + sat)) : (lig + sat - lig * sat);
        double sl2 = 2.0 * lig - sl1;
        double tmp[3] = { hue + 1.0 / 3.0, hue, hue - 1.0 / 3.0 };
        double rgb[3];

        for (int i = 0; i < 3; i++) {
            if (tmp[i] < 0.0) tmp[i] += 1.0;
            else if (tmp[i] > 1.0) tmp[i] -= 1.0;

            if (6.0 * tmp[i] < 1.0) {
                rgb[i] = sl2 + (sl1 - sl2) * 6.0 * tmp[i];
            } else if (2.0 * tmp[i] < 1.0) {
                rgb[i] = sl1;
            } else if (3.0 * tmp[i] < 2.0) {
                rgb[i] = sl2 + (sl1 - sl2) * (2.0 / 3.0 - tmp[i]) * 6.0;
            } else {
                rgb[i] = sl2;
            }
        }
        red = rgb[0];
        green = rgb[1];
        blue = rgb[2];
    }

    *r = (uint8_t)round(red * 255.0);
    *g = (uint8_t)round(green * 255.0);
    *b = (uint8_t)round(blue * 255.0);
}

/**
 * @brief Verifica se um valor de matiz está contido no intervalo `[hi, hs]`.
 * @details Suporta intervalos circulares (quando `hi > hs`, atravessando 360° -> 0°)
 *          e trata os extremos 0° e 360° como equivalentes no círculo cromático.
 *
 * @param[in] hue Valor da matiz do pixel (0 a 360).
 * @param[in] hi  Limite inicial do intervalo de matiz (0 a 360).
 * @param[in] hs  Limite final do intervalo de matiz (0 a 360).
 * @return 1 se a matiz pertencer ao intervalo, ou 0 caso contrário.
 */
static int matiz_no_intervalo(int hue, int hi, int hs)
{
    int dentro;
    if (hi > hs) {
        dentro = (hue >= hi || hue <= hs);
    } else {
        dentro = (hue >= hi && hue <= hs);
    }
    if (!dentro && (hue == 0 || hue == 360)) {
        int alt = (hue == 0) ? 360 : 0;
        if (hi > hs) {
            dentro = (alt >= hi || alt <= hs);
        } else {
            dentro = (alt >= hi && alt <= hs);
        }
    }
    return dentro;
}

/**
 * @brief Descolore um pixel RGB caso seus valores HSL estejam fora do intervalo especificado.
 * @details Quando o pixel não atende simultaneamente aos critérios de Matiz, Saturação e
 *          Luminosidade definidos em `interv`, suas componentes R, G e B são substituídas
 *          pela luminância perceptual ITU-R BT.709 (`Y = 0.2126*R + 0.7152*G + 0.0722*B`).
 *
 * @param[in,out] r      Ponteiro para a componente vermelha (Red) do pixel.
 * @param[in,out] g      Ponteiro para a componente verde (Green) do pixel.
 * @param[in,out] b      Ponteiro para a componente azul (Blue) do pixel.
 * @param[in]     interv Intervalo HSL que define as cores a serem preservadas.
 */
static void descolorir_pixel(uint8_t *r, uint8_t *g, uint8_t *b, IntervaloHSL interv)
{
    HSL hsl = rgb_para_hsl(*r, *g, *b);

    int fora_matiz = !matiz_no_intervalo(hsl.hue, interv.hi, interv.hs);
    int fora_saturacao = ((interv.si > interv.ss) && (hsl.saturation < interv.si && hsl.saturation > interv.ss)) ||
                         ((interv.si <= interv.ss) && (hsl.saturation < interv.si || hsl.saturation > interv.ss));
    int fora_luminosidade = ((interv.li > interv.ls) && (hsl.lightness < interv.li && hsl.lightness > interv.ls)) ||
                            ((interv.li <= interv.ls) && (hsl.lightness < interv.li || hsl.lightness > interv.ls));

    if (fora_matiz || fora_saturacao || fora_luminosidade) {
        uint8_t v = (uint8_t)round(*r * 0.2126 + *g * 0.7152 + *b * 0.0722);
        *r = v;
        *g = v;
        *b = v;
    }
}

/**
 * @brief Aplica a descoloração seletiva a todos os pixels de uma estrutura `Imagem`.
 *
 * @param[in,out] img    Ponteiro para a imagem cujos pixels serão processados in-place.
 * @param[in]     interv Intervalo HSL das cores que devem permanecer coloridas.
 */
static void descolorir_imagem(Imagem *img, IntervaloHSL interv)
{
    size_t total_pixels = (size_t)img->largura * (size_t)img->altura;
    int canais = img->canais;
    for (size_t i = 0; i < total_pixels; i++) {
        uint8_t *px = &img->pixels[i * canais];
        descolorir_pixel(&px[0], &px[1], &px[2], interv);
    }
}

/**
 * @brief Identifica o formato da imagem a partir da extensão do nome do arquivo (case-insensitive).
 *
 * @param[in] nome_arquivo Caminho ou nome do arquivo a ser analisado.
 * @return Valor correspondente da enumeração `FormatoImagem`, ou `FORMATO_DESCONHECIDO` se não reconhecido.
 */
static FormatoImagem detectar_formato_extensao(const char *nome_arquivo)
{
    const char *ext = strrchr(nome_arquivo, '.');
    if (!ext) {
        return FORMATO_DESCONHECIDO;
    }
    if (strcasecmp(ext, ".png") == 0) {
        return FORMATO_PNG;
    }
    if (strcasecmp(ext, ".tif") == 0 || strcasecmp(ext, ".tiff") == 0) {
        return FORMATO_TIFF;
    }
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return FORMATO_JPEG;
    }
    if (strcasecmp(ext, ".gif") == 0) {
        return FORMATO_GIF;
    }
    if (strcasecmp(ext, ".webp") == 0) {
        return FORMATO_WEBP;
    }
    if (strcasecmp(ext, ".bmp") == 0) {
        return FORMATO_BMP;
    }
    if (strcasecmp(ext, ".ppm") == 0 || strcasecmp(ext, ".pgm") == 0 || strcasecmp(ext, ".pnm") == 0) {
        return FORMATO_PPM;
    }
    return FORMATO_DESCONHECIDO;
}

/**
 * @brief Detecta o formato do arquivo de entrada pelos bytes de assinatura (magic bytes) ou pela extensão.
 * @details Inspeciona os primeiros 12 bytes do arquivo para identificar assinaturas binárias
 *          (PNG, TIFF, JPEG, GIF, WebP, BMP, Netpbm) e utiliza `detectar_formato_extensao` como fallback.
 *
 * @param[in] nome_arquivo Caminho do arquivo de entrada.
 * @return Formato identificado (`FormatoImagem`).
 */
static FormatoImagem detectar_formato_entrada(const char *nome_arquivo)
{
    unsigned char header[12];
    FILE *fp = fopen(nome_arquivo, "rb");
    if (!fp) {
        erro_fatal("Não foi possível abrir o arquivo de entrada '%s'.", nome_arquivo);
    }

    size_t lidos = fread(header, 1, sizeof(header), fp);
    fclose(fp);

    if (lidos >= 8 && png_sig_cmp(header, 0, 8) == 0) {
        return FORMATO_PNG;
    }
    if (lidos >= 4 &&
        ((header[0] == 'I' && header[1] == 'I' && header[2] == 42 && header[3] == 0) ||
         (header[0] == 'M' && header[1] == 'M' && header[2] == 0 && header[3] == 42))) {
        return FORMATO_TIFF;
    }
    if (lidos >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        return FORMATO_JPEG;
    }
    if (lidos >= 6 && (memcmp(header, "GIF87a", 6) == 0 || memcmp(header, "GIF89a", 6) == 0)) {
        return FORMATO_GIF;
    }
    if (lidos >= 12 && memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WEBP", 4) == 0) {
        return FORMATO_WEBP;
    }
    if (lidos >= 2 && header[0] == 'B' && header[1] == 'M') {
        return FORMATO_BMP;
    }
    if (lidos >= 3 && header[0] == 'P' &&
        (header[1] == '2' || header[1] == '3' || header[1] == '5' || header[1] == '6') &&
        (isspace((unsigned char)header[2]) || header[2] == '#')) {
        return FORMATO_PPM;
    }

    return detectar_formato_extensao(nome_arquivo);
}

/* ==========================================================================
 * Leitura e Escrita de PNG
 * ========================================================================== */

/**
 * @brief Lê um arquivo de imagem PNG e o decodifica para a estrutura `Imagem` (RGB ou RGBA de 8 bits).
 * @details Converte automaticamente imagens de 16 bits por canal, paletizadas (indexed),
 *          tons de cinza (1, 2, 4 ou 8 bits) e transparência tRNS para RGB (3 canais) ou RGBA (4 canais).
 *
 * @param[in] arquivo_entrada Caminho do arquivo PNG de entrada.
 * @return Estrutura `Imagem` preenchida com os pixels decodificados.
 */
static Imagem ler_png(const char *arquivo_entrada)
{
    unsigned char header[8];
    FILE *fp = fopen(arquivo_entrada, "rb");
    if (!fp) {
        erro_fatal("[PNG] Não foi possível abrir '%s' para leitura.", arquivo_entrada);
    }

    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8) != 0) {
        fclose(fp);
        erro_fatal("[PNG] O arquivo '%s' não é um arquivo PNG válido.", arquivo_entrada);
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        erro_fatal("[PNG] Falha em png_create_read_struct.");
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        erro_fatal("[PNG] Falha em png_create_info_struct.");
    }

    uint8_t * volatile pixels_alloc = NULL;
    png_bytep * volatile row_pointers = NULL;

    if (setjmp(png_jmpbuf(png_ptr))) {
        free((void *)row_pointers);
        free((void *)pixels_alloc);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        erro_fatal("[PNG] Erro durante a leitura da imagem PNG.");
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    uint32_t width = png_get_image_width(png_ptr, info_ptr);
    uint32_t height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    color_type = png_get_color_type(png_ptr, info_ptr);
    int canais = (color_type == PNG_COLOR_TYPE_RGBA) ? 4 : 3;
    size_t rowbytes = (size_t)width * (size_t)canais;

    Imagem img;
    img.largura = width;
    img.altura = height;
    img.canais = canais;
    pixels_alloc = (uint8_t *)malloc(rowbytes * height);
    if (!pixels_alloc) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        erro_fatal("[PNG] Falha ao alocar memória para os pixels.");
    }
    img.pixels = (uint8_t *)pixels_alloc;

    row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        free(img.pixels);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        erro_fatal("[PNG] Falha ao alocar ponteiros de linha.");
    }

    for (uint32_t y = 0; y < height; y++) {
        row_pointers[y] = img.pixels + (size_t)y * rowbytes;
    }

    png_read_image(png_ptr, (png_bytep *)row_pointers);
    free((void *)row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    return img;
}

/**
 * @brief Grava uma estrutura `Imagem` em um arquivo no formato PNG.
 *
 * @param[in] arquivo_saida    Caminho do arquivo PNG de saída.
 * @param[in] img              Ponteiro para a estrutura `Imagem` (RGB ou RGBA) a ser gravada.
 * @param[in] nivel_compressao Nível de compressão deflate da `libpng` (0 = sem compressão a 9 = máxima).
 */
static void salvar_png(const char *arquivo_saida, const Imagem *img, int nivel_compressao)
{
    FILE *fp = fopen(arquivo_saida, "wb");
    if (!fp) {
        erro_fatal("[PNG] Não foi possível abrir '%s' para escrita.", arquivo_saida);
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        erro_fatal("[PNG] Falha em png_create_write_struct.");
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        erro_fatal("[PNG] Falha em png_create_info_struct.");
    }

    png_bytep * volatile row_pointers = NULL;

    if (setjmp(png_jmpbuf(png_ptr))) {
        free((void *)row_pointers);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        erro_fatal("[PNG] Erro durante a escrita da imagem PNG.");
    }

    png_init_io(png_ptr, fp);
    png_set_compression_level(png_ptr, nivel_compressao);

    png_byte color_type = (img->canais == 4) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
    png_set_IHDR(png_ptr, info_ptr, img->largura, img->altura,
                 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    size_t rowbytes = (size_t)img->largura * (size_t)img->canais;
    row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * img->altura);
    if (!row_pointers) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        erro_fatal("[PNG] Falha ao alocar ponteiros de linha para escrita.");
    }

    for (uint32_t y = 0; y < img->altura; y++) {
        row_pointers[y] = img->pixels + (size_t)y * rowbytes;
    }

    png_write_image(png_ptr, (png_bytep *)row_pointers);
    png_write_end(png_ptr, NULL);

    free((void *)row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

/* ==========================================================================
 * Leitura e Escrita de TIFF
 * ========================================================================== */

/**
 * @brief Lê um arquivo de imagem TIFF e o decodifica para a estrutura `Imagem` (RGB ou RGBA de 8 bits).
 * @details Utiliza `TIFFReadRGBAImageOriented` com orientação `ORIENTATION_TOPLEFT` para suportar
 *          diferentes esquemas de compressão, subamostragem e organização planar.
 *
 * @param[in] arquivo_entrada Caminho do arquivo TIFF de entrada.
 * @return Estrutura `Imagem` preenchida com os pixels decodificados.
 */
static Imagem ler_tiff(const char *arquivo_entrada)
{
    TIFF *tif = TIFFOpen(arquivo_entrada, "r");
    if (!tif) {
        erro_fatal("[TIFF] Não foi possível abrir '%s' para leitura.", arquivo_entrada);
    }

    uint32_t width = 0, height = 0;
    uint16_t samples_per_pixel = 3;

    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height)) {
        TIFFClose(tif);
        erro_fatal("[TIFF] Não foi possível obter as dimensões de '%s'.", arquivo_entrada);
    }
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel);

    size_t npixels = (size_t)width * (size_t)height;
    uint32_t *raster = (uint32_t *)_TIFFmalloc(npixels * sizeof(uint32_t));
    if (!raster) {
        TIFFClose(tif);
        erro_fatal("[TIFF] Falha ao alocar memória para o raster.");
    }

    if (!TIFFReadRGBAImageOriented(tif, width, height, raster, ORIENTATION_TOPLEFT, 0)) {
        _TIFFfree(raster);
        TIFFClose(tif);
        erro_fatal("[TIFF] Falha ao ler os pixels de '%s'.", arquivo_entrada);
    }
    TIFFClose(tif);

    int canais = (samples_per_pixel == 4) ? 4 : 3;
    Imagem img;
    img.largura = width;
    img.altura = height;
    img.canais = canais;
    img.pixels = (uint8_t *)malloc(npixels * canais);
    if (!img.pixels) {
        _TIFFfree(raster);
        erro_fatal("[TIFF] Falha ao alocar memória para os pixels.");
    }

    for (size_t i = 0; i < npixels; i++) {
        uint32_t pixel = raster[i];
        img.pixels[i * canais + 0] = (uint8_t)TIFFGetR(pixel);
        img.pixels[i * canais + 1] = (uint8_t)TIFFGetG(pixel);
        img.pixels[i * canais + 2] = (uint8_t)TIFFGetB(pixel);
        if (canais == 4) {
            img.pixels[i * canais + 3] = (uint8_t)TIFFGetA(pixel);
        }
    }

    _TIFFfree(raster);
    return img;
}

/**
 * @brief Grava uma estrutura `Imagem` em um arquivo no formato TIFF.
 *
 * @param[in] arquivo_saida   Caminho do arquivo TIFF de saída.
 * @param[in] img             Ponteiro para a estrutura `Imagem` (RGB ou RGBA) a ser gravada.
 * @param[in] compressao_tiff Esquema de compressão da `libtiff` (ex.: `COMPRESSION_NONE`, `COMPRESSION_LZW`,
 *                            `COMPRESSION_ADOBE_DEFLATE`, `COMPRESSION_PACKBITS`).
 */
static void salvar_tiff(const char *arquivo_saida, const Imagem *img, uint16_t compressao_tiff)
{
    TIFF *out = TIFFOpen(arquivo_saida, "w");
    if (!out) {
        erro_fatal("[TIFF] Não foi possível abrir '%s' para escrita.", arquivo_saida);
    }

    int out_samples = img->canais;
    TIFFSetField(out, TIFFTAG_IMAGEWIDTH, img->largura);
    TIFFSetField(out, TIFFTAG_IMAGELENGTH, img->altura);
    TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, out_samples);
    TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(out, TIFFTAG_COMPRESSION, compressao_tiff);
    if (out_samples == 4) {
        uint16_t extra_samples[1] = { EXTRASAMPLE_UNASSALPHA };
        TIFFSetField(out, TIFFTAG_EXTRASAMPLES, 1, extra_samples);
    }
    TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, img->largura * out_samples));

    size_t rowbytes = (size_t)img->largura * (size_t)out_samples;
    for (uint32_t y = 0; y < img->altura; y++) {
        uint8_t *linha = img->pixels + (size_t)y * rowbytes;
        if (TIFFWriteScanline(out, linha, y, 0) < 0) {
            TIFFClose(out);
            erro_fatal("[TIFF] Erro ao gravar a linha %u em '%s'.", y, arquivo_saida);
        }
    }

    TIFFClose(out);
}

/* ==========================================================================
 * Leitura e Escrita de JPEG
 * ========================================================================== */

/**
 * @brief Lê um arquivo de imagem JPEG e o decodifica para a estrutura `Imagem` (RGB de 8 bits).
 *
 * @param[in] arquivo_entrada Caminho do arquivo JPEG de entrada.
 * @return Estrutura `Imagem` preenchida com 3 canais (RGB).
 */
static Imagem ler_jpeg(const char *arquivo_entrada)
{
    FILE *fp = fopen(arquivo_entrada, "rb");
    if (!fp) {
        erro_fatal("[JPEG] Não foi possível abrir '%s' para leitura.", arquivo_entrada);
    }

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        erro_fatal("[JPEG] O arquivo '%s' não possui um cabeçalho JPEG válido.", arquivo_entrada);
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    Imagem img;
    img.largura = cinfo.output_width;
    img.altura = cinfo.output_height;
    img.canais = 3;
    size_t row_stride = (size_t)img.largura * 3;

    img.pixels = (uint8_t *)malloc(row_stride * img.altura);
    if (!img.pixels) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        erro_fatal("[JPEG] Falha ao alocar memória para a imagem.");
    }

    while (cinfo.output_scanline < img.altura) {
        unsigned char *linha = img.pixels + (size_t)cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &linha, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    return img;
}

/**
 * @brief Grava uma estrutura `Imagem` em um arquivo no formato JPEG.
 * @details Caso a imagem de origem possua 4 canais (RGBA), o canal Alpha é descartado
 *          automaticamente durante a conversão de cada linha para RGB (3 canais).
 *
 * @param[in] arquivo_saida Caminho do arquivo JPEG de saída.
 * @param[in] img           Ponteiro para a estrutura `Imagem` a ser gravada.
 * @param[in] qualidade     Fator de qualidade da compressão JPEG (1 a 100).
 */
static void salvar_jpeg(const char *arquivo_saida, const Imagem *img, int qualidade)
{
    FILE *fp = fopen(arquivo_saida, "wb");
    if (!fp) {
        erro_fatal("[JPEG] Não foi possível abrir '%s' para escrita.", arquivo_saida);
    }

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    cinfo.image_width = img->largura;
    cinfo.image_height = img->altura;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, qualidade, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    if (img->canais == 3) {
        size_t row_stride = (size_t)img->largura * 3;
        while (cinfo.next_scanline < img->altura) {
            unsigned char *linha = img->pixels + (size_t)cinfo.next_scanline * row_stride;
            jpeg_write_scanlines(&cinfo, &linha, 1);
        }
    } else {
        /* Converte linhas RGBA (4 canais) para RGB (3 canais) ao gravar JPEG */
        unsigned char *linha_rgb = (unsigned char *)malloc((size_t)img->largura * 3);
        if (!linha_rgb) {
            jpeg_destroy_compress(&cinfo);
            fclose(fp);
            erro_fatal("[JPEG] Falha ao alocar buffer de linha RGB.");
        }
        while (cinfo.next_scanline < img->altura) {
            const uint8_t *orig = img->pixels + (size_t)cinfo.next_scanline * img->largura * 4;
            for (uint32_t x = 0; x < img->largura; x++) {
                linha_rgb[x * 3 + 0] = orig[x * 4 + 0];
                linha_rgb[x * 3 + 1] = orig[x * 4 + 1];
                linha_rgb[x * 3 + 2] = orig[x * 4 + 2];
            }
            jpeg_write_scanlines(&cinfo, &linha_rgb, 1);
        }
        free(linha_rgb);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(fp);
}

/* ==========================================================================
 * Leitura e Escrita de GIF
 * ========================================================================== */

/**
 * @brief Lê o primeiro quadro de um arquivo GIF e o decodifica para a estrutura `Imagem` (RGB ou RGBA).
 * @details Consulta o `GraphicsControlBlock` (GCB) para identificar se há índice de cor transparente;
 *          caso exista, gera uma imagem RGBA de 4 canais com Alpha = 0 nos pixels transparentes.
 *
 * @param[in] arquivo_entrada Caminho do arquivo GIF de entrada.
 * @return Estrutura `Imagem` preenchida com os pixels decodificados.
 */
static Imagem ler_gif(const char *arquivo_entrada)
{
    int erro = 0;
    GifFileType *gif = DGifOpenFileName(arquivo_entrada, &erro);
    if (!gif) {
        erro_fatal("[GIF] Não foi possível abrir '%s' para leitura (%s).",
                   arquivo_entrada, GifErrorString(erro));
    }

    if (DGifSlurp(gif) != GIF_OK || gif->ImageCount < 1) {
        int err_leitura = gif->Error;
        DGifCloseFile(gif, &erro);
        erro_fatal("[GIF] Falha ao ler os dados de '%s' (%s).",
                   arquivo_entrada, GifErrorString(err_leitura));
    }

    SavedImage *frame = &gif->SavedImages[0];
    ColorMapObject *cmap = frame->ImageDesc.ColorMap ? frame->ImageDesc.ColorMap : gif->SColorMap;
    if (!cmap) {
        DGifCloseFile(gif, &erro);
        erro_fatal("[GIF] O arquivo '%s' não possui tabela de cores.", arquivo_entrada);
    }

    GraphicsControlBlock gcb;
    int cor_transparente = NO_TRANSPARENT_COLOR;
    if (DGifSavedExtensionToGCB(gif, 0, &gcb) == GIF_OK) {
        cor_transparente = gcb.TransparentColor;
    }

    uint32_t width = (uint32_t)frame->ImageDesc.Width;
    uint32_t height = (uint32_t)frame->ImageDesc.Height;
    size_t npixels = (size_t)width * (size_t)height;
    int canais = (cor_transparente != NO_TRANSPARENT_COLOR) ? 4 : 3;

    Imagem img;
    img.largura = width;
    img.altura = height;
    img.canais = canais;
    img.pixels = (uint8_t *)malloc(npixels * (size_t)canais);
    if (!img.pixels) {
        DGifCloseFile(gif, &erro);
        erro_fatal("[GIF] Falha ao alocar memória para os pixels.");
    }

    for (size_t i = 0; i < npixels; i++) {
        int idx = frame->RasterBits[i];
        if (idx >= 0 && idx < cmap->ColorCount) {
            img.pixels[i * canais + 0] = cmap->Colors[idx].Red;
            img.pixels[i * canais + 1] = cmap->Colors[idx].Green;
            img.pixels[i * canais + 2] = cmap->Colors[idx].Blue;
        } else {
            img.pixels[i * canais + 0] = 0;
            img.pixels[i * canais + 1] = 0;
            img.pixels[i * canais + 2] = 0;
        }
        if (canais == 4) {
            img.pixels[i * 4 + 3] = (idx == cor_transparente) ? 0 : 255;
        }
    }

    DGifCloseFile(gif, &erro);
    return img;
}

/**
 * @brief Grava uma estrutura `Imagem` em um arquivo no formato GIF89a utilizando quantização de cores.
 * @details Reduz o espaço de cores RGB de 24 bits para uma paleta indexada de até 256 cores
 *          por meio de `GifQuantizeBuffer`.
 *
 * @param[in] arquivo_saida Caminho do arquivo GIF de saída.
 * @param[in] img           Ponteiro para a estrutura `Imagem` a ser gravada.
 */
static void salvar_gif(const char *arquivo_saida, const Imagem *img)
{
    size_t npixels = (size_t)img->largura * (size_t)img->altura;
    GifByteType *r_buf = (GifByteType *)malloc(npixels);
    GifByteType *g_buf = (GifByteType *)malloc(npixels);
    GifByteType *b_buf = (GifByteType *)malloc(npixels);
    GifByteType *out_raster = (GifByteType *)malloc(npixels);

    if (!r_buf || !g_buf || !b_buf || !out_raster) {
        free(r_buf);
        free(g_buf);
        free(b_buf);
        free(out_raster);
        erro_fatal("[GIF] Falha ao alocar buffers para quantização GIF.");
    }

    int canais = img->canais;
    for (size_t i = 0; i < npixels; i++) {
        r_buf[i] = img->pixels[i * canais + 0];
        g_buf[i] = img->pixels[i * canais + 1];
        b_buf[i] = img->pixels[i * canais + 2];
    }

    int map_size = 256;
    ColorMapObject *cmap = GifMakeMapObject(map_size, NULL);
    if (!cmap) {
        free(r_buf);
        free(g_buf);
        free(b_buf);
        free(out_raster);
        erro_fatal("[GIF] Falha ao criar mapa de cores.");
    }

    if (GifQuantizeBuffer(img->largura, img->altura, &map_size,
                          r_buf, g_buf, b_buf, out_raster, cmap->Colors) == GIF_ERROR) {
        GifFreeMapObject(cmap);
        free(r_buf);
        free(g_buf);
        free(b_buf);
        free(out_raster);
        erro_fatal("[GIF] Falha ao quantizar cores para o formato GIF.");
    }

    free(r_buf);
    free(g_buf);
    free(b_buf);

    int erro = 0;
    GifFileType *gif_out = EGifOpenFileName(arquivo_saida, false, &erro);
    if (!gif_out) {
        GifFreeMapObject(cmap);
        free(out_raster);
        erro_fatal("[GIF] Não foi possível abrir '%s' para escrita (%s).",
                   arquivo_saida, GifErrorString(erro));
    }

    EGifSetGifVersion(gif_out, true);
    if (EGifPutScreenDesc(gif_out, img->largura, img->altura, 8, 0, cmap) == GIF_ERROR ||
        EGifPutImageDesc(gif_out, 0, 0, img->largura, img->altura, false, NULL) == GIF_ERROR) {
        GifFreeMapObject(cmap);
        free(out_raster);
        EGifCloseFile(gif_out, &erro);
        erro_fatal("[GIF] Erro ao gravar cabeçalhos em '%s'.", arquivo_saida);
    }

    for (uint32_t y = 0; y < img->altura; y++) {
        if (EGifPutLine(gif_out, out_raster + (size_t)y * img->largura, img->largura) == GIF_ERROR) {
            GifFreeMapObject(cmap);
            free(out_raster);
            EGifCloseFile(gif_out, &erro);
            erro_fatal("[GIF] Erro ao gravar linha %u em '%s'.", y, arquivo_saida);
        }
    }

    GifFreeMapObject(cmap);
    free(out_raster);
    EGifCloseFile(gif_out, &erro);
}

/**
 * @brief Descolore seletivamente uma tabela de cores (`ColorMapObject`) de um arquivo GIF.
 *
 * @param[in,out] cmap   Ponteiro para o mapa de cores (global ou local) do GIF.
 * @param[in]     interv Intervalo HSL das cores que devem ser mantidas.
 */
static void descolorir_mapa_cores_gif(ColorMapObject *cmap, IntervaloHSL interv)
{
    if (!cmap || !cmap->Colors) {
        return;
    }
    for (int i = 0; i < cmap->ColorCount; i++) {
        uint8_t r = cmap->Colors[i].Red;
        uint8_t g = cmap->Colors[i].Green;
        uint8_t b = cmap->Colors[i].Blue;
        descolorir_pixel(&r, &g, &b, interv);
        cmap->Colors[i].Red = r;
        cmap->Colors[i].Green = g;
        cmap->Colors[i].Blue = b;
    }
}

/**
 * @brief Processa diretamente um arquivo GIF de entrada para um arquivo GIF de saída.
 * @details Modifica apenas as paletas de cores global (`SColorMap`) e locais de cada quadro,
 *          preservando integralmente múltiplos quadros (animações), tempos de exibição,
 *          blocos de extensão e índices de transparência sem perda por requantização.
 *
 * @param[in] arquivo_entrada Caminho do arquivo GIF de entrada.
 * @param[in] arquivo_saida   Caminho do arquivo GIF de saída.
 * @param[in] interv          Intervalo HSL das cores que devem permanecer coloridas.
 */
static void processar_gif_direto(const char *arquivo_entrada, const char *arquivo_saida, IntervaloHSL interv)
{
    int erro = 0;
    GifFileType *gif_in = DGifOpenFileName(arquivo_entrada, &erro);
    if (!gif_in) {
        erro_fatal("[GIF] Não foi possível abrir '%s' para leitura (%s).",
                   arquivo_entrada, GifErrorString(erro));
    }

    if (DGifSlurp(gif_in) != GIF_OK) {
        int err_leitura = gif_in->Error;
        DGifCloseFile(gif_in, &erro);
        erro_fatal("[GIF] Falha ao ler os dados de '%s' (%s).",
                   arquivo_entrada, GifErrorString(err_leitura));
    }

    descolorir_mapa_cores_gif(gif_in->SColorMap, interv);
    for (int i = 0; i < gif_in->ImageCount; i++) {
        descolorir_mapa_cores_gif(gif_in->SavedImages[i].ImageDesc.ColorMap, interv);
    }

    GifFileType *gif_out = EGifOpenFileName(arquivo_saida, false, &erro);
    if (!gif_out) {
        DGifCloseFile(gif_in, &erro);
        erro_fatal("[GIF] Não foi possível abrir '%s' para escrita (%s).",
                   arquivo_saida, GifErrorString(erro));
    }

    gif_out->SWidth = gif_in->SWidth;
    gif_out->SHeight = gif_in->SHeight;
    gif_out->SColorResolution = gif_in->SColorResolution;
    gif_out->SBackGroundColor = gif_in->SBackGroundColor;
    gif_out->AspectByte = gif_in->AspectByte;
    gif_out->SColorMap = gif_in->SColorMap;
    gif_out->ImageCount = gif_in->ImageCount;
    gif_out->SavedImages = gif_in->SavedImages;
    gif_out->ExtensionBlockCount = gif_in->ExtensionBlockCount;
    gif_out->ExtensionBlocks = gif_in->ExtensionBlocks;

    gif_in->SColorMap = NULL;
    gif_in->SavedImages = NULL;
    gif_in->ImageCount = 0;
    gif_in->ExtensionBlocks = NULL;
    gif_in->ExtensionBlockCount = 0;

    if (EGifSpew(gif_out, &erro) != GIF_OK) {
        DGifCloseFile(gif_in, &erro);
        erro_fatal("[GIF] Erro ao gravar '%s' (%s).",
                   arquivo_saida, GifErrorString(erro));
    }

    DGifCloseFile(gif_in, &erro);
}

/* ==========================================================================
 * Leitura e Escrita de WebP
 * ========================================================================== */

/**
 * @brief Lê um arquivo de imagem WebP e o decodifica para a estrutura `Imagem` (RGBA de 8 bits).
 *
 * @param[in] arquivo_entrada Caminho do arquivo WebP de entrada.
 * @return Estrutura `Imagem` preenchida com 4 canais (RGBA).
 */
static Imagem ler_webp(const char *arquivo_entrada)
{
    FILE *fp = fopen(arquivo_entrada, "rb");
    if (!fp) {
        erro_fatal("[WebP] Não foi possível abrir '%s' para leitura.", arquivo_entrada);
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        erro_fatal("[WebP] Erro ao determinar o tamanho de '%s'.", arquivo_entrada);
    }
    long tam = ftell(fp);
    if (tam <= 0) {
        fclose(fp);
        erro_fatal("[WebP] Arquivo '%s' inválido ou vazio.", arquivo_entrada);
    }
    rewind(fp);

    uint8_t *dados = (uint8_t *)malloc((size_t)tam);
    if (!dados) {
        fclose(fp);
        erro_fatal("[WebP] Falha ao alocar memória para leitura de '%s'.", arquivo_entrada);
    }

    if (fread(dados, 1, (size_t)tam, fp) != (size_t)tam) {
        free(dados);
        fclose(fp);
        erro_fatal("[WebP] Erro ao ler os bytes de '%s'.", arquivo_entrada);
    }
    fclose(fp);

    int width = 0, height = 0;
    uint8_t *decoded = WebPDecodeRGBA(dados, (size_t)tam, &width, &height);
    free(dados);

    if (!decoded || width <= 0 || height <= 0) {
        if (decoded) WebPFree(decoded);
        erro_fatal("[WebP] Falha ao decodificar '%s'.", arquivo_entrada);
    }

    size_t total_bytes = (size_t)width * (size_t)height * 4;
    Imagem img;
    img.largura = (uint32_t)width;
    img.altura = (uint32_t)height;
    img.canais = 4;
    img.pixels = (uint8_t *)malloc(total_bytes);
    if (!img.pixels) {
        WebPFree(decoded);
        erro_fatal("[WebP] Falha ao alocar memória para os pixels.");
    }

    memcpy(img.pixels, decoded, total_bytes);
    WebPFree(decoded);
    return img;
}

/**
 * @brief Grava uma estrutura `Imagem` em um arquivo no formato WebP (com perdas ou sem perdas).
 *
 * @param[in] arquivo_saida Caminho do arquivo WebP de saída.
 * @param[in] img           Ponteiro para a estrutura `Imagem` (RGB ou RGBA) a ser gravada.
 * @param[in] qualidade     Fator de qualidade (1 a 100) usado quando `lossless == 0`.
 * @param[in] lossless      Se diferente de 0, codifica no modo sem perdas (`WebPEncodeLossless*`).
 */
static void salvar_webp(const char *arquivo_saida, const Imagem *img, int qualidade, int lossless)
{
    uint8_t *saida = NULL;
    size_t tam = 0;
    int stride = (int)img->largura * img->canais;

    if (lossless) {
        if (img->canais == 4) {
            tam = WebPEncodeLosslessRGBA(img->pixels, (int)img->largura, (int)img->altura,
                                         stride, &saida);
        } else {
            tam = WebPEncodeLosslessRGB(img->pixels, (int)img->largura, (int)img->altura,
                                        stride, &saida);
        }
    } else {
        if (img->canais == 4) {
            tam = WebPEncodeRGBA(img->pixels, (int)img->largura, (int)img->altura,
                                 stride, (float)qualidade, &saida);
        } else {
            tam = WebPEncodeRGB(img->pixels, (int)img->largura, (int)img->altura,
                                stride, (float)qualidade, &saida);
        }
    }

    if (tam == 0 || !saida) {
        if (saida) WebPFree(saida);
        erro_fatal("[WebP] Falha ao codificar imagem para '%s'.", arquivo_saida);
    }

    FILE *fp = fopen(arquivo_saida, "wb");
    if (!fp) {
        WebPFree(saida);
        erro_fatal("[WebP] Não foi possível abrir '%s' para escrita.", arquivo_saida);
    }

    if (fwrite(saida, 1, tam, fp) != tam) {
        WebPFree(saida);
        fclose(fp);
        erro_fatal("[WebP] Erro ao gravar os bytes em '%s'.", arquivo_saida);
    }

    WebPFree(saida);
    fclose(fp);
}

/* ==========================================================================
 * Leitura e Escrita de BMP
 * ========================================================================== */

/**
 * @brief Lê um inteiro sem sinal de 16 bits em formato little-endian a partir de um buffer de bytes.
 *
 * @param[in] p Ponteiro para pelo menos 2 bytes.
 * @return Valor `uint16_t` decodificado.
 */
static uint16_t ler_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/**
 * @brief Lê um inteiro sem sinal de 32 bits em formato little-endian a partir de um buffer de bytes.
 *
 * @param[in] p Ponteiro para pelo menos 4 bytes.
 * @return Valor `uint32_t` decodificado.
 */
static uint32_t ler_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief Lê um inteiro com sinal de 32 bits em formato little-endian a partir de um buffer de bytes.
 *
 * @param[in] p Ponteiro para pelo menos 4 bytes.
 * @return Valor `int32_t` decodificado.
 */
static int32_t ler_i32_le(const uint8_t *p)
{
    return (int32_t)ler_u32_le(p);
}

/**
 * @brief Escreve um inteiro sem sinal de 16 bits em formato little-endian em um buffer de bytes.
 *
 * @param[out] p Ponteiro para o buffer de no mínimo 2 bytes.
 * @param[in]  v Valor `uint16_t` a ser gravado.
 */
static void escrever_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/**
 * @brief Escreve um inteiro sem sinal de 32 bits em formato little-endian em um buffer de bytes.
 *
 * @param[out] p Ponteiro para o buffer de no mínimo 4 bytes.
 * @param[in]  v Valor `uint32_t` a ser gravado.
 */
static void escrever_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/**
 * @brief Lê um arquivo de imagem BMP (24 ou 32 bits sem compressão) e o decodifica para `Imagem`.
 * @details Suporta imagens orientadas de baixo para cima (bottom-up, altura positiva) e de cima
 *          para baixo (top-down, altura negativa), convertendo a ordem de canais BGR/BGRA para RGB/RGBA.
 *
 * @param[in] arquivo_entrada Caminho do arquivo BMP de entrada.
 * @return Estrutura `Imagem` preenchida com 3 (RGB) ou 4 (RGBA) canais.
 */
static Imagem ler_bmp(const char *arquivo_entrada)
{
    FILE *fp = fopen(arquivo_entrada, "rb");
    if (!fp) {
        erro_fatal("[BMP] Não foi possível abrir '%s' para leitura.", arquivo_entrada);
    }

    uint8_t file_header[14];
    if (fread(file_header, 1, 14, fp) != 14 || file_header[0] != 'B' || file_header[1] != 'M') {
        fclose(fp);
        erro_fatal("[BMP] O arquivo '%s' não possui cabeçalho BMP válido.", arquivo_entrada);
    }

    uint32_t data_offset = ler_u32_le(&file_header[10]);

    uint8_t info_header[40];
    if (fread(info_header, 1, 40, fp) != 40) {
        fclose(fp);
        erro_fatal("[BMP] Cabeçalho DIB incompleto em '%s'.", arquivo_entrada);
    }

    int32_t width = ler_i32_le(&info_header[4]);
    int32_t height_raw = ler_i32_le(&info_header[8]);
    uint16_t bpp = ler_u16_le(&info_header[14]);
    uint32_t compression = ler_u32_le(&info_header[16]);

    if (width <= 0 || height_raw == 0) {
        fclose(fp);
        erro_fatal("[BMP] Dimensões inválidas em '%s'.", arquivo_entrada);
    }
    if ((bpp != 24 && bpp != 32) || (compression != 0 && !(bpp == 32 && compression == 3))) {
        fclose(fp);
        erro_fatal("[BMP] Apenas arquivos BMP de 24 ou 32 bits sem compressão são suportados ('%s').",
                   arquivo_entrada);
    }

    int top_down = (height_raw < 0);
    int64_t height_abs = top_down ? -(int64_t)height_raw : (int64_t)height_raw;
    uint32_t height = (uint32_t)height_abs;
    int canais = (bpp == 32) ? 4 : 3;
    size_t row_padded = (((size_t)width * (size_t)canais + 3) / 4) * 4;

    if (fseek(fp, (long)data_offset, SEEK_SET) != 0) {
        fclose(fp);
        erro_fatal("[BMP] Erro ao posicionar nos dados de imagem em '%s'.", arquivo_entrada);
    }

    Imagem img;
    img.largura = (uint32_t)width;
    img.altura = height;
    img.canais = canais;
    img.pixels = (uint8_t *)malloc((size_t)width * height * canais);
    uint8_t *linha_bmp = (uint8_t *)malloc(row_padded);

    if (!img.pixels || !linha_bmp) {
        free(img.pixels);
        free(linha_bmp);
        fclose(fp);
        erro_fatal("[BMP] Falha ao alocar memória para os pixels.");
    }

    for (uint32_t y = 0; y < height; y++) {
        if (fread(linha_bmp, 1, row_padded, fp) != row_padded) {
            free(img.pixels);
            free(linha_bmp);
            fclose(fp);
            erro_fatal("[BMP] Fim inesperado do arquivo ao ler a linha %u de '%s'.", y, arquivo_entrada);
        }
        uint32_t dest_y = top_down ? y : (height - 1 - y);
        uint8_t *dest = img.pixels + (size_t)dest_y * (size_t)width * canais;
        for (uint32_t x = 0; x < (uint32_t)width; x++) {
            dest[x * canais + 0] = linha_bmp[x * canais + 2]; /* R */
            dest[x * canais + 1] = linha_bmp[x * canais + 1]; /* G */
            dest[x * canais + 2] = linha_bmp[x * canais + 0]; /* B */
            if (canais == 4) {
                dest[x * canais + 3] = linha_bmp[x * canais + 3]; /* A */
            }
        }
    }

    free(linha_bmp);
    fclose(fp);
    return img;
}

/**
 * @brief Grava uma estrutura `Imagem` em um arquivo no formato BMP (24 bits BGR ou 32 bits BGRA).
 * @details Escreve cabeçalho `BITMAPFILEHEADER` (14 bytes) e `BITMAPINFOHEADER` (40 bytes),
 *          aplicando alinhamento de linha (padding) para múltiplos de 4 bytes.
 *
 * @param[in] arquivo_saida Caminho do arquivo BMP de saída.
 * @param[in] img           Ponteiro para a estrutura `Imagem` (RGB ou RGBA) a ser gravada.
 */
static void salvar_bmp(const char *arquivo_saida, const Imagem *img)
{
    FILE *fp = fopen(arquivo_saida, "wb");
    if (!fp) {
        erro_fatal("[BMP] Não foi possível abrir '%s' para escrita.", arquivo_saida);
    }

    int canais = img->canais;
    size_t row_padded = (((size_t)img->largura * (size_t)canais + 3) / 4) * 4;
    uint32_t data_size = (uint32_t)(row_padded * img->altura);
    uint32_t file_size = 54 + data_size;

    uint8_t header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    escrever_u32_le(&header[2], file_size);
    escrever_u32_le(&header[10], 54);
    escrever_u32_le(&header[14], 40);
    escrever_u32_le(&header[18], img->largura);
    escrever_u32_le(&header[22], img->altura);
    escrever_u16_le(&header[26], 1);
    escrever_u16_le(&header[28], (uint16_t)(canais * 8));
    escrever_u32_le(&header[34], data_size);

    if (fwrite(header, 1, 54, fp) != 54) {
        fclose(fp);
        erro_fatal("[BMP] Erro ao gravar cabeçalho em '%s'.", arquivo_saida);
    }

    uint8_t *linha_bmp = (uint8_t *)calloc(1, row_padded);
    if (!linha_bmp) {
        fclose(fp);
        erro_fatal("[BMP] Falha ao alocar buffer de linha.");
    }

    for (uint32_t y = 0; y < img->altura; y++) {
        uint32_t src_y = img->altura - 1 - y;
        const uint8_t *src = img->pixels + (size_t)src_y * (size_t)img->largura * canais;
        for (uint32_t x = 0; x < img->largura; x++) {
            linha_bmp[x * canais + 0] = src[x * canais + 2]; /* B */
            linha_bmp[x * canais + 1] = src[x * canais + 1]; /* G */
            linha_bmp[x * canais + 2] = src[x * canais + 0]; /* R */
            if (canais == 4) {
                linha_bmp[x * canais + 3] = src[x * canais + 3]; /* A */
            }
        }
        if (fwrite(linha_bmp, 1, row_padded, fp) != row_padded) {
            free(linha_bmp);
            fclose(fp);
            erro_fatal("[BMP] Erro ao gravar linha %u em '%s'.", y, arquivo_saida);
        }
    }

    free(linha_bmp);
    fclose(fp);
}

/* ==========================================================================
 * Leitura e Escrita de PPM / PGM (Netpbm)
 * ========================================================================== */

/**
 * @brief Lê o próximo token de texto de um fluxo Netpbm (PPM/PGM), ignorando espaços e comentários `#`.
 *
 * @param[in,out] fp  Ponteiro para o arquivo aberto em leitura.
 * @param[out]    buf Buffer onde o token lido será armazenado (terminado por `'\0'`).
 * @param[in]     tam Tamanho máximo do buffer `buf` em bytes.
 * @return 1 se um token foi lido com sucesso, ou 0 se atingiu o fim do arquivo (`EOF`).
 */
static int ler_token_ppm(FILE *fp, char *buf, size_t tam)
{
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (isspace((unsigned char)c)) {
            continue;
        }
        if (c == '#') {
            while ((c = fgetc(fp)) != EOF && c != '\n' && c != '\r') {
                /* Ignora comentários */
            }
            continue;
        }
        break;
    }
    if (c == EOF) {
        return 0;
    }

    size_t pos = 0;
    while (c != EOF && !isspace((unsigned char)c) && c != '#') {
        if (pos + 1 < tam) {
            buf[pos++] = (char)c;
        }
        c = fgetc(fp);
    }
    if (c == '#') {
        while ((c = fgetc(fp)) != EOF && c != '\n' && c != '\r') {
            /* Ignora restante do comentário */
        }
    }
    buf[pos] = '\0';
    return 1;
}

/**
 * @brief Lê um arquivo Netpbm nos formatos P2 (PGM ASCII), P3 (PPM ASCII), P5 (PGM binário) ou P6 (PPM binário).
 * @details Suporta valores máximos (`maxval`) de 1 a 65535 (8 ou 16 bits por amostra), normalizando
 *          todas as amostras para RGB de 8 bits por canal (0..255).
 *
 * @param[in] arquivo_entrada Caminho do arquivo PPM/PGM/PNM de entrada.
 * @return Estrutura `Imagem` preenchida com 3 canais (RGB).
 */
static Imagem ler_ppm(const char *arquivo_entrada)
{
    FILE *fp = fopen(arquivo_entrada, "rb");
    if (!fp) {
        erro_fatal("[PPM] Não foi possível abrir '%s' para leitura.", arquivo_entrada);
    }

    char magic[16], tok_w[32], tok_h[32], tok_max[32];
    if (!ler_token_ppm(fp, magic, sizeof(magic)) ||
        !ler_token_ppm(fp, tok_w, sizeof(tok_w)) ||
        !ler_token_ppm(fp, tok_h, sizeof(tok_h)) ||
        !ler_token_ppm(fp, tok_max, sizeof(tok_max))) {
        fclose(fp);
        erro_fatal("[PPM] Cabeçalho Netpbm inválido em '%s'.", arquivo_entrada);
    }

    int tipo = 0;
    if (strcmp(magic, "P6") == 0) tipo = 6;
    else if (strcmp(magic, "P3") == 0) tipo = 3;
    else if (strcmp(magic, "P5") == 0) tipo = 5;
    else if (strcmp(magic, "P2") == 0) tipo = 2;
    else {
        fclose(fp);
        erro_fatal("[PPM] Tipo Netpbm '%s' não suportado em '%s' (use P2, P3, P5 ou P6).",
                   magic, arquivo_entrada);
    }

    long width = strtol(tok_w, NULL, 10);
    long height = strtol(tok_h, NULL, 10);
    long maxval = strtol(tok_max, NULL, 10);

    if (width <= 0 || height <= 0 || maxval <= 0 || maxval > 65535) {
        fclose(fp);
        erro_fatal("[PPM] Dimensões ou valor máximo inválidos em '%s'.", arquivo_entrada);
    }

    size_t npixels = (size_t)width * (size_t)height;
    Imagem img;
    img.largura = (uint32_t)width;
    img.altura = (uint32_t)height;
    img.canais = 3;
    img.pixels = (uint8_t *)malloc(npixels * 3);
    if (!img.pixels) {
        fclose(fp);
        erro_fatal("[PPM] Falha ao alocar memória para os pixels.");
    }

    if (tipo == 6 && maxval == 255) {
        if (fread(img.pixels, 1, npixels * 3, fp) != npixels * 3) {
            free(img.pixels);
            fclose(fp);
            erro_fatal("[PPM] Dados binários incompletos em '%s'.", arquivo_entrada);
        }
    } else if (tipo == 5 && maxval == 255) {
        for (size_t i = 0; i < npixels; i++) {
            int v = fgetc(fp);
            if (v == EOF) {
                free(img.pixels);
                fclose(fp);
                erro_fatal("[PPM] Dados binários PGM incompletos em '%s'.", arquivo_entrada);
            }
            img.pixels[i * 3 + 0] = (uint8_t)v;
            img.pixels[i * 3 + 1] = (uint8_t)v;
            img.pixels[i * 3 + 2] = (uint8_t)v;
        }
    } else if (tipo == 6 || tipo == 5) {
        int canais_in = (tipo == 6) ? 3 : 1;
        for (size_t i = 0; i < npixels; i++) {
            uint8_t rgb[3] = {0, 0, 0};
            for (int c = 0; c < canais_in; c++) {
                int val = fgetc(fp);
                if (val == EOF) {
                    free(img.pixels);
                    fclose(fp);
                    erro_fatal("[PPM] Dados binários incompletos em '%s'.", arquivo_entrada);
                }
                if (maxval > 255) {
                    int lo = fgetc(fp);
                    if (lo == EOF) {
                        free(img.pixels);
                        fclose(fp);
                        erro_fatal("[PPM] Dados binários de 16 bits incompletos em '%s'.", arquivo_entrada);
                    }
                    val = (val << 8) | (lo & 0xFF);
                }
                if (val > maxval) val = (int)maxval;
                rgb[c] = (uint8_t)round(((double)val * 255.0) / (double)maxval);
            }
            if (canais_in == 1) {
                rgb[1] = rgb[2] = rgb[0];
            }
            img.pixels[i * 3 + 0] = rgb[0];
            img.pixels[i * 3 + 1] = rgb[1];
            img.pixels[i * 3 + 2] = rgb[2];
        }
    } else {
        int canais_in = (tipo == 3) ? 3 : 1;
        char tok[32];
        for (size_t i = 0; i < npixels; i++) {
            uint8_t rgb[3] = {0, 0, 0};
            for (int c = 0; c < canais_in; c++) {
                if (!ler_token_ppm(fp, tok, sizeof(tok))) {
                    free(img.pixels);
                    fclose(fp);
                    erro_fatal("[PPM] Dados ASCII incompletos em '%s'.", arquivo_entrada);
                }
                long val = strtol(tok, NULL, 10);
                if (val < 0) val = 0;
                if (val > maxval) val = maxval;
                rgb[c] = (uint8_t)round(((double)val * 255.0) / (double)maxval);
            }
            if (canais_in == 1) {
                rgb[1] = rgb[2] = rgb[0];
            }
            img.pixels[i * 3 + 0] = rgb[0];
            img.pixels[i * 3 + 1] = rgb[1];
            img.pixels[i * 3 + 2] = rgb[2];
        }
    }

    fclose(fp);
    return img;
}

/**
 * @brief Grava uma estrutura `Imagem` em um arquivo Netpbm (PPM colorido ou PGM em escala de cinza).
 * @details Se a extensão de `arquivo_saida` for `.pgm`, grava como PGM (P5 binário ou P2 ASCII);
 *          caso contrário, grava como PPM (P6 binário ou P3 ASCII).
 *
 * @param[in] arquivo_saida Caminho do arquivo PPM/PGM/PNM de saída.
 * @param[in] img           Ponteiro para a estrutura `Imagem` a ser gravada.
 * @param[in] ascii         Se diferente de 0, grava em formato texto ASCII (P2/P3); caso contrário, binário (P5/P6).
 */
static void salvar_ppm(const char *arquivo_saida, const Imagem *img, int ascii)
{
    FILE *fp = fopen(arquivo_saida, "wb");
    if (!fp) {
        erro_fatal("[PPM] Não foi possível abrir '%s' para escrita.", arquivo_saida);
    }

    const char *ext = strrchr(arquivo_saida, '.');
    int salvar_como_pgm = (ext && strcasecmp(ext, ".pgm") == 0);

    size_t npixels = (size_t)img->largura * (size_t)img->altura;
    int canais = img->canais;

    if (salvar_como_pgm) {
        fprintf(fp, "%s\n%u %u\n255\n", ascii ? "P2" : "P5", img->largura, img->altura);
        for (size_t i = 0; i < npixels; i++) {
            uint8_t r = img->pixels[i * canais + 0];
            uint8_t g = img->pixels[i * canais + 1];
            uint8_t b = img->pixels[i * canais + 2];
            uint8_t v = (uint8_t)round(r * 0.2126 + g * 0.7152 + b * 0.0722);
            if (ascii) {
                fprintf(fp, "%u%c", v, ((i + 1) % img->largura == 0) ? '\n' : ' ');
            } else {
                fputc(v, fp);
            }
        }
    } else if (ascii) {
        fprintf(fp, "P3\n%u %u\n255\n", img->largura, img->altura);
        for (size_t i = 0; i < npixels; i++) {
            fprintf(fp, "%u %u %u%c",
                    img->pixels[i * canais + 0],
                    img->pixels[i * canais + 1],
                    img->pixels[i * canais + 2],
                    ((i + 1) % img->largura == 0) ? '\n' : ' ');
        }
    } else {
        fprintf(fp, "P6\n%u %u\n255\n", img->largura, img->altura);
        if (canais == 3) {
            if (fwrite(img->pixels, 1, npixels * 3, fp) != npixels * 3) {
                fclose(fp);
                erro_fatal("[PPM] Erro ao gravar dados em '%s'.", arquivo_saida);
            }
        } else {
            for (size_t i = 0; i < npixels; i++) {
                fwrite(&img->pixels[i * 4], 1, 3, fp);
            }
        }
    }

    fclose(fp);
}

/**
 * @brief Converte o nome textual de um algoritmo de compressão TIFF na constante correspondente da `libtiff`.
 *
 * @param[in] str Nome do esquema de compressão (`"none"`, `"lzw"`, `"deflate"`, `"zip"` ou `"packbits"`).
 * @return Código de compressão (`uint16_t`) da `libtiff`.
 */
static uint16_t interpretar_compressao_tiff(const char *str)
{
    if (strcasecmp(str, "none") == 0) {
        return COMPRESSION_NONE;
    }
    if (strcasecmp(str, "lzw") == 0) {
        return COMPRESSION_LZW;
    }
    if (strcasecmp(str, "deflate") == 0 || strcasecmp(str, "zip") == 0) {
        return COMPRESSION_ADOBE_DEFLATE;
    }
    if (strcasecmp(str, "packbits") == 0) {
        return COMPRESSION_PACKBITS;
    }
    erro_fatal("Compressão TIFF desconhecida: '%s' (valores aceitos: none, lzw, deflate, zip, packbits).", str);
    return COMPRESSION_NONE;
}

/**
 * @brief Monta uma régua numérica textual alinhada proporcionalmente ao número de colunas do gradiente.
 *
 * @param[out] regua   Buffer de caracteres de tamanho mínimo `colunas + 1`.
 * @param[in]  colunas Largura total da barra em colunas de caracteres.
 * @param[in]  passo   Incremento numérico entre as marcações da régua.
 * @param[in]  maximo  Valor numérico máximo no extremo direito da régua.
 */
static void montar_regua(char *regua, int colunas, int passo, int maximo)
{
    memset(regua, ' ', (size_t)colunas);
    regua[colunas] = '\0';

    for (int v = 0; v <= maximo; v += passo) {
        char num[16];
        int len = snprintf(num, sizeof(num), "%d", v);
        int col = (int)round(((double)v / (double)maximo) * (double)(colunas - 1));
        int inicio;

        if (v == 0) {
            inicio = 0;
        } else if (v == maximo) {
            inicio = colunas - len;
        } else {
            inicio = col - (len / 2);
        }

        if (inicio < 0) inicio = 0;
        if (inicio + len > colunas) inicio = colunas - len;

        memcpy(regua + inicio, num, (size_t)len);
    }
}

/**
 * @brief Exibe no terminal um diagrama colorido (ANSI TrueColor 24-bit) ilustrando o espaço de cores HSL.
 * @details Renderiza três blocos de 80 colunas:
 *          - 9 linhas de Matiz (`H: 0..360`) variando a luminosidade `L` de 10 a 90;
 *          - 6 linhas de Saturação (`S: 0..100`) para as matizes `H = 0, 60, 120, 180, 240, 300`;
 *          - 6 linhas de Luminosidade (`L: 0..100`) para as matizes `H = 0, 60, 120, 180, 240, 300`.
 *
 * @param[in,out] out Fluxo de saída onde o diagrama será impresso (ex.: `stderr` ou `stdout`).
 */
static void exibir_escala_hsl(FILE *out)
{
    const int colunas = 80;
    const int matizes[6] = { 0, 60, 120, 180, 240, 300 };
    char regua_h[colunas + 1];
    char regua_sl[colunas + 1];

    montar_regua(regua_h, colunas, 30, 360);
    montar_regua(regua_sl, colunas, 10, 100);

    fprintf(out, "\nEspaço de cores HSL (Hue: 0..360 | Saturation: 0..100 | Lightness: 0..100):\n");

    /* 1. Gradiente de Matiz (H: 0..360) em 9 linhas (L de 10 a 90) */
    fprintf(out, "H:  %s\n", regua_h);
    for (int i = 0; i < 9; i++) {
        int l_val = (i + 1) * 10;
        fprintf(out, "    ");
        for (int col = 0; col < colunas; col++) {
            double h = ((double)col / (double)(colunas - 1)) * 360.0;
            uint8_t r, g, b;
            hsl_para_rgb(h, 100.0, (double)l_val, &r, &g, &b);
            fprintf(out, "\033[48;2;%u;%u;%um ", r, g, b);
        }
        fprintf(out, "\033[0m (L=%d)\n", l_val);
    }

    /* 2. Gradiente de Saturação (S: 0..100) em 6 linhas (H = 0, 60, 120, 180, 240, 300) */
    fprintf(out, "\nS:  %s\n", regua_sl);
    for (int i = 0; i < 6; i++) {
        int h_val = matizes[i];
        fprintf(out, "    ");
        for (int col = 0; col < colunas; col++) {
            double s = ((double)col / (double)(colunas - 1)) * 100.0;
            uint8_t r, g, b;
            hsl_para_rgb((double)h_val, s, 50.0, &r, &g, &b);
            fprintf(out, "\033[48;2;%u;%u;%um ", r, g, b);
        }
        fprintf(out, "\033[0m (H=%d)\n", h_val);
    }

    /* 3. Gradiente de Luminosidade (L: 0..100) em 6 linhas (H = 0, 60, 120, 180, 240, 300) */
    fprintf(out, "\nL:  %s\n", regua_sl);
    for (int i = 0; i < 6; i++) {
        int h_val = matizes[i];
        fprintf(out, "    ");
        for (int col = 0; col < colunas; col++) {
            double l = ((double)col / (double)(colunas - 1)) * 100.0;
            uint8_t r, g, b;
            hsl_para_rgb((double)h_val, 100.0, l, &r, &g, &b);
            fprintf(out, "\033[48;2;%u;%u;%um ", r, g, b);
        }
        fprintf(out, "\033[0m (H=%d)\n", h_val);
    }
}

/**
 * @brief Exibe a mensagem de ajuda com a sintaxe de uso, opções da linha de comando e a escala HSL.
 *
 * @param[in] prog Nome ou caminho do executável (`argv[0]`).
 */
static void exibir_uso(const char *prog)
{
    fprintf(stderr,
            "Programa de Descoloração Seletiva.\n\n"
            "Uso:\n"
            "  %s <arquivo_entrada> -o <arquivo_saida> (-h <inicio> [<fim>] | -s <inicio> [<fim>] | -l <inicio> [<fim>]) [opções]\n\n"
            "Os parâmetros podem ser informados em qualquer ordem (respeitando apenas <inicio> [<fim>] após cada opção de intervalo).\n"
            "Se apenas um valor for informado em -h, -s ou -l, ele será usado tanto para <inicio> quanto para <fim>.\n"
            "Formatos de imagens suportados: PNG, TIFF, JPEG, GIF, WebP, BMP e PPM/PGM.\n\n"
            "Parâmetros obrigatórios:\n"
            "  <arquivo_entrada>                  Imagem de entrada\n"
            "  -o, --out <arquivo_saida>          Imagem de saída\n"
            "  Pelo menos um intervalo HSL (-h, -s ou -l)\n\n"
            "Intervalos HSL (informe ao menos um):\n"
            "  -h, --hue <inicio> [<fim>]         Intervalo de Matiz (Hue) de 0 a 360 (padrão: 0 360)\n"
            "  -s, --saturation <inicio> [<fim>]  Intervalo de Saturação (Saturation) de 0 a 100 (padrão: 100 100)\n"
            "  -l, --lightness <inicio> [<fim>]   Intervalo de Luminosidade (Lightness) de 0 a 100 (padrão: 50 50)\n\n"
            "Opções adicionais:\n"
            "  -q, --quality <qualidade>          Qualidade do JPEG/WebP de saída de 1 a 100 (padrão: 95)\n"
            "  -z, --png-level <nivel>            Nível de compressão do PNG de 0 a 9 (padrão: 6)\n"
            "  -c, --compression <tipo>           Compressão do TIFF: none, lzw, deflate, zip, packbits (padrão: none)\n"
            "      --lossless                     Grava WebP em modo sem perdas (lossless)\n"
            "      --ascii                        Grava PPM/PGM em modo texto ASCII (P3/P2)\n"
            "      --help                         Exibe esta mensagem de ajuda\n",
            prog);
    exibir_escala_hsl(stderr);
}

/**
 * @brief Verifica se uma string representa um número inteiro válido dentro dos limites do tipo `int`.
 *
 * @param[in] str String a ser verificada.
 * @return 1 se `str` for um número inteiro válido, ou 0 caso contrário.
 */
static int eh_inteiro(const char *str)
{
    if (!str || str[0] == '\0') {
        return 0;
    }
    char *fim = NULL;
    errno = 0;
    long val = strtol(str, &fim, 10);
    return (*fim == '\0' && errno != ERANGE && val >= INT_MIN && val <= INT_MAX);
}

/**
 * @brief Converte uma string em um número inteiro (`int`), encerrando o programa com erro se for inválida.
 *
 * @param[in] str        String contendo a representação textual do número.
 * @param[in] nome_param Nome da opção ou parâmetro associado (usado na mensagem de erro).
 * @return Valor inteiro convertido.
 */
static int ler_inteiro(const char *str, const char *nome_param)
{
    char *fim = NULL;
    errno = 0;
    long val = strtol(str, &fim, 10);
    if (str[0] == '\0' || *fim != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX) {
        erro_fatal("Valor inválido '%s' para %s (esperado um número inteiro válido).", str, nome_param);
    }
    return (int)val;
}

/**
 * @brief Função principal do programa de descoloração seletiva.
 * @details Analisa os argumentos de linha de comando, valida os parâmetros obrigatórios e limites HSL,
 *          detecta os formatos de entrada e saída, executa a descoloração seletiva e grava o arquivo resultante.
 *
 * @param[in] argc Quantidade de argumentos passados na linha de comando.
 * @param[in] argv Vetor de strings com os argumentos da linha de comando.
 * @return `EXIT_SUCCESS` (0) em caso de sucesso, ou `EXIT_FAILURE` em caso de erro.
 */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        exibir_uso(argv[0]);
        return EXIT_FAILURE;
    }

    const char *arquivo_entrada = NULL;
    const char *arquivo_saida = NULL;

    IntervaloHSL interv = { .hi = 0, .hs = 360, .si = 100, .ss = 100, .li = 50, .ls = 50 };
    int informou_intervalo = 0;
    int qualidade_jpeg = 95;
    int nivel_png = 6;
    uint16_t compressao_tiff = COMPRESSION_NONE;
    int webp_lossless = 0;
    int ppm_ascii = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            exibir_uso(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                erro_fatal("A opção '%s' requer o caminho do arquivo de saída.", argv[i]);
            }
            arquivo_saida = argv[i + 1];
            i += 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--hue") == 0) {
            if (i + 1 >= argc) {
                erro_fatal("A opção '%s' requer um ou dois valores: <inicio> [<fim>].", argv[i]);
            }
            interv.hi = ler_inteiro(argv[i + 1], argv[i]);
            if (i + 2 < argc && eh_inteiro(argv[i + 2])) {
                interv.hs = ler_inteiro(argv[i + 2], argv[i]);
                i += 2;
            } else {
                interv.hs = interv.hi;
                i += 1;
            }
            informou_intervalo = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--saturation") == 0) {
            if (i + 1 >= argc) {
                erro_fatal("A opção '%s' requer um ou dois valores: <inicio> [<fim>].", argv[i]);
            }
            interv.si = ler_inteiro(argv[i + 1], argv[i]);
            if (i + 2 < argc && eh_inteiro(argv[i + 2])) {
                interv.ss = ler_inteiro(argv[i + 2], argv[i]);
                i += 2;
            } else {
                interv.ss = interv.si;
                i += 1;
            }
            informou_intervalo = 1;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--lightness") == 0) {
            if (i + 1 >= argc) {
                erro_fatal("A opção '%s' requer um ou dois valores: <inicio> [<fim>].", argv[i]);
            }
            interv.li = ler_inteiro(argv[i + 1], argv[i]);
            if (i + 2 < argc && eh_inteiro(argv[i + 2])) {
                interv.ls = ler_inteiro(argv[i + 2], argv[i]);
                i += 2;
            } else {
                interv.ls = interv.li;
                i += 1;
            }
            informou_intervalo = 1;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quality") == 0) {
            if (i + 1 >= argc) {
                erro_fatal("A opção '%s' requer um valor: <qualidade>.", argv[i]);
            }
            qualidade_jpeg = ler_inteiro(argv[i + 1], argv[i]);
            i += 1;
        } else if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--png-level") == 0) {
            if (i + 1 >= argc) {
                erro_fatal("A opção '%s' requer um valor: <nivel>.", argv[i]);
            }
            nivel_png = ler_inteiro(argv[i + 1], argv[i]);
            i += 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--compression") == 0) {
            if (i + 1 >= argc) {
                erro_fatal("A opção '%s' requer um valor: <tipo>.", argv[i]);
            }
            compressao_tiff = interpretar_compressao_tiff(argv[i + 1]);
            i += 1;
        } else if (strcmp(argv[i], "--lossless") == 0) {
            webp_lossless = 1;
        } else if (strcmp(argv[i], "--ascii") == 0) {
            ppm_ascii = 1;
        } else if (argv[i][0] == '-') {
            erro_fatal("Opção desconhecida: '%s'. Use --help para ver as opções disponíveis.", argv[i]);
        } else if (!arquivo_entrada) {
            arquivo_entrada = argv[i];
        } else {
            erro_fatal("Argumento inesperado: '%s'. O arquivo de saída deve ser informado com -o ou --out.", argv[i]);
        }
    }

    if (!arquivo_entrada) {
        fprintf(stderr, "Erro: O arquivo de entrada é obrigatório.\n\n");
        exibir_uso(argv[0]);
        return EXIT_FAILURE;
    }
    if (!arquivo_saida) {
        fprintf(stderr, "Erro: O arquivo de saída (-o / --out) é obrigatório.\n\n");
        exibir_uso(argv[0]);
        return EXIT_FAILURE;
    }
    if (!informou_intervalo) {
        fprintf(stderr, "Erro: É obrigatório informar ao menos um intervalo HSL (-h/--hue, -s/--saturation ou -l/--lightness).\n\n");
        exibir_uso(argv[0]);
        return EXIT_FAILURE;
    }

    if (interv.hi < 0 || interv.hi > 360 || interv.hs < 0 || interv.hs > 360) {
        erro_fatal("O intervalo de matiz (-h/--hue) deve estar entre 0 e 360.");
    }
    if (interv.si < 0 || interv.si > 100 || interv.ss < 0 || interv.ss > 100) {
        erro_fatal("O intervalo de saturação (-s/--saturation) deve estar entre 0 e 100.");
    }
    if (interv.li < 0 || interv.li > 100 || interv.ls < 0 || interv.ls > 100) {
        erro_fatal("O intervalo de luminosidade (-l/--lightness) deve estar entre 0 e 100.");
    }
    if (qualidade_jpeg < 1 || qualidade_jpeg > 100) {
        erro_fatal("A qualidade (-q/--quality) deve estar entre 1 e 100 (informado: %d).", qualidade_jpeg);
    }
    if (nivel_png < 0 || nivel_png > 9) {
        erro_fatal("O nível de compressão do PNG (-z/--png-level) deve estar entre 0 e 9 (informado: %d).", nivel_png);
    }

    FormatoImagem formato_in = detectar_formato_entrada(arquivo_entrada);
    if (formato_in == FORMATO_DESCONHECIDO) {
        erro_fatal("Formato de arquivo não suportado para '%s'. Use PNG, TIFF, JPEG, GIF, WebP, BMP ou PPM/PGM.",
                   arquivo_entrada);
    }

    FormatoImagem formato_out = detectar_formato_extensao(arquivo_saida);
    if (formato_out == FORMATO_DESCONHECIDO) {
        formato_out = formato_in;
    }

    /* Caso especial GIF -> GIF: edita diretamente as paletas de cores */
    if (formato_in == FORMATO_GIF && formato_out == FORMATO_GIF) {
        processar_gif_direto(arquivo_entrada, arquivo_saida, interv);
        return EXIT_SUCCESS;
    }

    Imagem img;
    if (formato_in == FORMATO_PNG) {
        img = ler_png(arquivo_entrada);
    } else if (formato_in == FORMATO_TIFF) {
        img = ler_tiff(arquivo_entrada);
    } else if (formato_in == FORMATO_JPEG) {
        img = ler_jpeg(arquivo_entrada);
    } else if (formato_in == FORMATO_GIF) {
        img = ler_gif(arquivo_entrada);
    } else if (formato_in == FORMATO_WEBP) {
        img = ler_webp(arquivo_entrada);
    } else if (formato_in == FORMATO_BMP) {
        img = ler_bmp(arquivo_entrada);
    } else {
        img = ler_ppm(arquivo_entrada);
    }

    descolorir_imagem(&img, interv);

    if (formato_out == FORMATO_PNG) {
        salvar_png(arquivo_saida, &img, nivel_png);
    } else if (formato_out == FORMATO_TIFF) {
        salvar_tiff(arquivo_saida, &img, compressao_tiff);
    } else if (formato_out == FORMATO_JPEG) {
        salvar_jpeg(arquivo_saida, &img, qualidade_jpeg);
    } else if (formato_out == FORMATO_GIF) {
        salvar_gif(arquivo_saida, &img);
    } else if (formato_out == FORMATO_WEBP) {
        salvar_webp(arquivo_saida, &img, qualidade_jpeg, webp_lossless);
    } else if (formato_out == FORMATO_BMP) {
        salvar_bmp(arquivo_saida, &img);
    } else {
        salvar_ppm(arquivo_saida, &img, ppm_ascii);
    }

    liberar_imagem(&img);
    return EXIT_SUCCESS;
}

#!/usr/bin/env python3
"""Fills the Spanish rows of scummvm/engines/saga/itedata.cpp for Inherit the Earth's Spanish fan
translation (Game & Watch build): the texts ScummVM itself shows -- intro captions, the puzzle,
and the interface (as gwITEinterfaceES). In Latin-1, as that translation's text and font are.
Run once on a clean itedata.cpp; the result goes into patches/scummvm-slim.patch."""
import re, sys

P = sys.argv[1] if len(sys.argv) > 1 else 'scummvm/engines/saga/itedata.cpp'

def c(s):
    # a C string literal in Latin-1, with octal escapes (a hex escape would eat the next letters)
    out = ''
    for ch in s:
        b = ch.encode('latin-1')[0]
        if ch == '"': out += '\\"'
        elif ch == '\\': out += '\\\\'
        elif b >= 128: out += '\\%03o' % b
        else: out += ch
    return '"' + out + '"'

INTERFACE = [
    "Ir a", "Mirar", "Coger", "Hablar con", "Abrir",
    "Cerrar", "Usar", "Dar", "Opciones", "Prueba",
    "Demo", "Ayuda", "Salir del juego", "Rápida", "Lenta",
    "Sí", "No", "Seguir jugando", "Cargar", "Guardar",
    "Opciones del juego", "Velocidad de lectura", "Música", "Sonido", "Cancelar",
    "Salir", "Aceptar", "Media", "Clic", "10%",
    "20%", "30%", "40%", "50%", "60%",
    "70%", "80%", "90%", "Máx", "¿Salir del juego?",
    "¡Partida cargada!", "Nombre de la partida", "Dar %s a %s", "Usar %s con %s",
    "[Nueva partida]",
    "No puedo coger eso.",
    "No veo nada especial.",
    "No hay por dónde abrirlo.",
    "No hay nada que cerrar.",
    "No sé cómo hacer eso.",
    "Ver diálogos",
    "¿Qué responde Rif?",
    "Cargando una partida guardada",
]
assert len(INTERFACE) == 53

TABLES = {
    'pieceNames': ["destornillador", "alicates", "sargento", "prensa de madera", "nivel",
                   "cordel", "cepillo", "martillo", "cinta métrica", "hacha",
                   "tijeras", "regla", "sierra", "mazo", "pincel"],
    'hintStr': ["Mira primero qué piezas podrían ir en cada esquina.",
                "Mira qué esquina admite menos piezas y empieza por ella.",
                "Revisa cada esquina y cada lado nuevos buscando piezas que encajen.",
                "No veo nada fuera de su sitio."],
    'solicitStr': ["¡Eh, Zorro! ¿Quieres una pista?",
                   "¿Te echo una mano?",
                   "Mmm... Mmm...",
                   "¡Psst! ¿Quieres una pista?",
                   "Yo lo habría hecho de otra manera, ¿sabes?"],
    'sakkaStr': ["¡Eh, no debéis ayudar a los aspirantes!",
                 "¡Chicos! ¡Se supone que esto es una prueba!",
                 "¡Vamos, compañeros, eso no está en las reglas!"],
    'whineStr': ["¡Venga ya, Sakka!",
                 "Una pista no hará daño, ¿no?",
                 "Ay...",
                 "¡Creo que esa carpeta se te ha subido a la cabeza, Sakka!",
                 "Pues no recuerdo ninguna regla que prohíba las pistas."],
    'optionsStr': ["\"Ya haré este puzle luego.\"",
                   "\"Sí, una pista, por favor.\"",
                   "\"No, gracias, quiero intentarlo yo solo.\"",
                   "Creo que la pieza \"%s\" est\u00e1 mal colocada."],
}

CAVES = {
    'introDialogueCave1': [(0, "Vemos el cielo, vemos la tierra, vemos el agua, y nos preguntamos: ¿somos los únicos?"),
                           (1, "Mucho antes de que existiéramos, los humanos gobernaban la Tierra."),
                           (2, "Hacían cosas maravillosas y movían montañas enteras."),
                           (3, "Conocían el Secreto del Vuelo, el Secreto de la Felicidad y otros secretos que ni imaginamos.")],
    'introDialogueCave2': [(4, "Los humanos conocían también el Secreto de la Vida, y lo usaron para darnos los Cuatro Grandes Dones:"),
                           (5, "Mentes que piensan, corazones que sienten, bocas que hablan y manos que alcanzan."),
                           (6, "Somos sus hijos.")],
    'introDialogueCave3': [(7, "Nos enseñaron a usar las manos, y a hablar."),
                           (8, "Nos mostraron la alegría de usar la mente."),
                           (9, "Nos querían, y cuando estuviéramos preparados, sin duda nos habrían dado el Secreto de la Felicidad.")],
    'introDialogueCave4': [(10, "Y ahora vemos el cielo, la tierra y el agua que hemos heredado, y nos preguntamos: ¿por qué se fueron?"),
                           (11, "¿Siguen vivos, en las estrellas? ¿En lo hondo de los océanos? ¿En el viento?"),
                           (12, "Nos preguntamos: ¿tuvieron un buen o un mal final?"),
                           (13, "¿Y correremos nosotros la misma suerte algún día?")],
}

s = open(P, encoding='latin-1').read()
if 'gwITEinterfaceES' in s:
    sys.exit('already done')

def table_span(name):
    i = s.index(name)
    j = s.index('\n};', i)
    return i, j

for name, rows in TABLES.items():
    i, j = table_span(name)
    body = s[i:j]
    m = re.search(r'\t// Spanish\n\t\{\n(.*?)\n\t\},', body, re.S)
    assert m, name
    new = '\t// Spanish (Game & Watch: for the fan translation, in Latin-1)\n\t{\n' + ',\n'.join('\t\t' + c(r) for r in rows) + '\n\t},'
    s = s[:i] + body[:m.start()] + new + body[m.end():] + s[j:]

for name, rows in CAVES.items():
    i, j = table_span(name)
    body = s[i:j]
    m = re.search(r'\t\{ \{ // Spanish\n.*?\} \},', body, re.S)
    assert m, name
    parts = ['\t\t%d,\t\t// cave voice %d\n\t\t%s\n\t}' % (v, v, c(t)) for v, t in rows]
    new = '\t{ { // Spanish (Game & Watch: for the fan translation, in Latin-1)\n' + ',\n\t{\n'.join(parts) + ' },'
    s = s[:i] + body[:m.start()] + new + body[m.end():] + s[j:]

i = s.index('const char *pieceNames')
s = s[:i] + ('#ifdef GW_SAGA_SLIM\n// Game & Watch: the interface in Spanish, for ITE\'s Spanish fan translation (Latin-1)\n'
             'const char *gwITEinterfaceES[53] = {\n' + ',\n'.join('\t' + c(r) for r in INTERFACE) + '\n};\n#endif\n\n') + s[i:]
open(P, 'w', encoding='latin-1').write(s)
print('ok')

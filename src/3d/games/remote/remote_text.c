/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The texts of TELETRABAJO: CÁMARA APAGADA, one row each in English,
 * Spanish and Portuguese (hd_lang). Spanish is the reference: it is written
 * first and the others follow it. Translators touch only this file.
 */
#include "remote.h"

static const char *const texts[RT_COUNT][LANGS] = {
   [RT_TITLE] = { "REMOTE WORK: CAMERA OFF", "TELETRABAJO: CÁMARA APAGADA", "TELETRABALHO: CÂMERA DESLIGADA" },
   [RT_HINT_WALK] = { "Stick: walk   X/Y: run   L/R: camera", "Stick: caminar   X/Y: correr   L/R: cámara", "Analógico: andar   X/Y: correr   L/R: câmera" },
   [RT_WORK] = { "Work", "Trabajo", "Trabalho" },
   [RT_ENERGY] = { "Energy", "Energía", "Energia" },
   [RT_POINTS] = { "Points", "Puntos", "Pontos" },
   [RT_STOP] = { "A: stop", "A: dejarlo", "A: parar" },
   [RT_ASLEEP] = { "Asleep on the keyboard. Zzz...", "Dormido sobre el teclado. Zzz...", "Dormindo no teclado. Zzz..." },

   [RT_PROMPT + RA_WORK] = { "A: Work (sigh)", "A: Trabajar (suspiro)", "A: Trabalhar (suspiro)" },
   [RT_PROMPT + RA_NAP] = { "A: Take a nap", "A: Echarse una siesta", "A: Tirar uma soneca" },
   [RT_PROMPT + RA_COFFEE] = { "A: Make a coffee", "A: Prepararse un café", "A: Fazer um café" },
   [RT_PROMPT + RA_COOK] = { "A: Cook lunch", "A: Cocinar el almuerzo", "A: Cozinhar o almoço" },
   [RT_PROMPT + RA_FRIDGE] = { "A: Raid the fridge", "A: Asaltar la heladera", "A: Atacar a geladeira" },
   [RT_PROMPT + RA_EAT] = { "A: Eat at the table", "A: Comer en la mesa", "A: Comer na mesa" },
   [RT_PROMPT + RA_SERIES] = { "A: Just one episode", "A: Solo un capítulo", "A: Só um episódio" },
   [RT_PROMPT + RA_CONSOLE] = { "A: Play the console", "A: Jugar a la consola", "A: Jogar videogame" },
   [RT_PROMPT + RA_GUITAR] = { "A: Play the guitar", "A: Tocar la guitarra", "A: Tocar violão" },
   [RT_PROMPT + RA_SUN] = { "A: Sunbathe", "A: Tomar sol", "A: Tomar sol" },
   [RT_PROMPT + RA_PLANTS] = { "A: Water the plants", "A: Regar las plantas", "A: Regar as plantas" },
   [RT_PROMPT + RA_SHOWER] = { "A: Have a shower", "A: Darse una ducha", "A: Tomar um banho" },
   [RT_PROMPT + RA_WASHING] = { "A: Put on the washing", "A: Poner un lavarropas", "A: Ligar a máquina de lavar" },

   [RT_DOING + RA_WORK] = { "Working. Allegedly.", "Trabajando. Supuestamente.", "Trabalhando. Supostamente." },
   [RT_DOING + RA_NAP] = { "Napping...", "Durmiendo la siesta...", "Tirando uma soneca..." },
   [RT_DOING + RA_COFFEE] = { "Making coffee...", "Preparando café...", "Fazendo café..." },
   [RT_DOING + RA_COOK] = { "Cooking...", "Cocinando...", "Cozinhando..." },
   [RT_DOING + RA_FRIDGE] = { "Looking for something...", "Buscando algo rico...", "Procurando algo gostoso..." },
   [RT_DOING + RA_EAT] = { "Eating...", "Comiendo...", "Comendo..." },
   [RT_DOING + RA_SERIES] = { "Watching a series...", "Mirando una serie...", "Vendo uma série..." },
   [RT_DOING + RA_CONSOLE] = { "Playing...", "Jugando...", "Jogando..." },
   [RT_DOING + RA_GUITAR] = { "Playing the guitar...", "Tocando la guitarra...", "Tocando violão..." },
   [RT_DOING + RA_SUN] = { "Sunbathing...", "Tomando sol...", "Tomando sol..." },
   [RT_DOING + RA_PLANTS] = { "Watering...", "Regando...", "Regando..." },
   [RT_DOING + RA_SHOWER] = { "In the shower...", "En la ducha...", "No banho..." },
   [RT_DOING + RA_WASHING] = { "Loading the washing...", "Cargando el lavarropas...", "Enchendo a máquina..." },

   [RT_DONE + RA_WORK] = { "Some work got done. Don't get used to it.", "Algo se trabajó. Que no se haga costumbre.", "Algo foi feito. Não se acostume." },
   [RT_DONE + RA_NAP] = { "Productivity: doubtful. Rest: impeccable.", "Productividad: dudosa. Descanso: impecable.", "Produtividade: duvidosa. Descanso: impecável." },
   [RT_DONE + RA_COFFEE] = { "Another coffee. Who's counting?", "Otro café. ¿Quién lleva la cuenta?", "Mais um café. Quem está contando?" },
   [RT_DONE + RA_COOK] = { "Lunch at 11. Bold.", "Almuerzo a las 11. Audaz.", "Almoço às 11. Ousado." },
   [RT_DONE + RA_FRIDGE] = { "The fridge is still empty. Again.", "La heladera sigue vacía. Otra vez.", "A geladeira continua vazia. De novo." },
   [RT_DONE + RA_EAT] = { "Lunch without a camera. Luxury.", "Almorzar sin cámara. Un lujo.", "Almoçar sem câmera. Um luxo." },
   [RT_DONE + RA_SERIES] = { "It was not just one episode.", "No fue solo un capítulo.", "Não foi só um episódio." },
   [RT_DONE + RA_CONSOLE] = { "Level passed. Ticket not.", "Pasaste el nivel. El ticket no.", "Passou de fase. O ticket não." },
   [RT_DONE + RA_GUITAR] = { "The neighbours loved it. Not really.", "Los vecinos aplauden. Mentira.", "Os vizinhos adoraram. Mentira." },
   [RT_DONE + RA_SUN] = { "Vitamin D: up. Status: away.", "Vitamina D: arriba. Estado: ausente.", "Vitamina D: em alta. Status: ausente." },
   [RT_DONE + RA_PLANTS] = { "The plants thank you. The boss doesn't.", "Las plantas te lo agradecen. El jefe no.", "As plantas agradecem. O chefe não." },
   [RT_DONE + RA_SHOWER] = { "Clean, fresh and still not working.", "Limpio, fresco y sin trabajar.", "Limpo, fresco e sem trabalhar." },
   [RT_DONE + RA_WASHING] = { "The washing machine works. Someone does.", "El lavarropas trabaja. Alguien lo hace.", "A máquina trabalha. Alguém trabalha." },
   [RT_AGAIN] = { "Again? It is not that fun anymore.", "¿Otra vez? Ya no es tan divertido.", "De novo? Já não tem tanta graça." },
};

const char *remote_text(int32_t id)
{
   if (id < 0 || id >= RT_COUNT || !texts[id][0])
      return "";
   return texts[id][hd_clamp(hd_lang, 0, LANGS - 1)];
}

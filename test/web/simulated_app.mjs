// The desktop app's Rust side, simulated for the card page (docs/index.html): used by the page's
// tests (card_page.test.mjs) and, injected into a browser, by tools/screenshots.mjs. The function
// refers to nothing outside itself, so its source runs in a page as it is.
//
// As app/src-tauri gives it to the page: list_cards, open_card with a channel for the card's lines,
// send (the answers come back over the channel, line by line) and close_card; register_older (its
// stages, app.registerStages, over a channel, then app.registerOutcome; "wait": until cancel_older)
// and cancel_older for older-camera pairing; app.registerError: the word register_older fails with. An
// answer can be a function of app (for a state that follows the commands sent). openError: the word
// open_card fails with. unplug(): the card's port ends with a word, as when the card is unplugged.
export function simulatedApp(paths, answers, { openError = null } = {}) {
  const app = {
    calls: [], channel: null, registerOutcome: "accepted", registerError: null,
    registerStages: ["on-network", "registering"], registered: [],
  };
  const core = {
    Channel: class {
      constructor() { this.onmessage = null; }
    },
    invoke: async (command, args = {}) => {
      app.calls.push([command, args.path ?? args.command].filter((part) => part !== undefined).join(" "));
      if (command === "list_cards") return paths.map((path) => ({ path }));
      if (command === "open_card") {
        if (openError) throw openError; // Tauri rejects with the command's error value
        app.channel = args.onEvent;
        return null;
      }
      if (command === "send") {
        const answer = typeof answers[args.command] === "function" ? answers[args.command](app) : answers[args.command];
        const lines = (answer || []).join("").split("\n").map((line) => line.replace(/\r$/, ""));
        for (const text of lines.filter(Boolean)) setTimeout(() => app.channel.onmessage({ kind: "line", text }), 5);
        return null;
      }
      if (command === "register_older") {
        if (app.registerError) throw app.registerError; // before anything is sent, as the app checks first
        app.registered.push(`${args.identity} ${args.name}`);
        for (const kind of app.registerStages) setTimeout(() => args.onEvent.onmessage({ kind }), 5);
        if (app.registerOutcome === "wait") return new Promise((resolve) => { app.stopRegister = resolve; });
        return new Promise((resolve) => setTimeout(() => resolve(app.registerOutcome), 20));
      }
      if (command === "cancel_older") {
        if (app.stopRegister) app.stopRegister("cancelled");
        return null;
      }
      if (command === "close_card") {
        setTimeout(() => app.channel.onmessage({ kind: "closed", error: null }), 1);
        return null;
      }
      throw new Error(`no such command: ${command}`);
    },
  };
  app.window = { isSecureContext: true, __TAURI__: { core } };
  app.unplug = (word = "port-failed") => app.channel.onmessage({ kind: "closed", error: word });
  return app;
}

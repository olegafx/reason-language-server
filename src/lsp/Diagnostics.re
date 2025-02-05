open TopTypes;
open Infix;
open Result;
open Log;

let makeDiagnostic = (documentText, ((line, c1, c2), message)) => {
  open Rpc.J;
  let text = String.concat("\n", message);
  let (l2, c22) =
    {
      let%opt lineOff = PartialParser.offsetOfLine(documentText, line);
      let off2 = lineOff + c2;
      PartialParser.offsetToPosition(documentText, off2);
    }
    |? (line, c2);
  o([
    ("range", Protocol.rangeOfInts(line, c1, l2, c22)),
    ("message", s(text)),
    ("severity", i(Utils.startsWith(text, "Warning") ? 2 : 1)),
  ]);
};

let runDiagnostics = (uri, state, ~package) => {
  Log.log("Running diagnostics for " ++ uri);
  let%try_consume (documentText, _verison, _isClean) = MessageHandlers.maybeHash(state.documentText, uri) |> orError("No document text found");
  let%try_consume result = State.getCompilationResult(uri, state, ~package);

  open Rpc.J;
  Rpc.sendNotification(log, stdout, "textDocument/publishDiagnostics", o([
    ("uri", s(uri)),
    ("diagnostics", switch result {
    | AsYouType.SyntaxError(text, otherText, _) => {
      let errors = AsYouType.parseErrors(Utils.splitLines(Utils.stripAnsii(otherText)));
      let errors = errors |. Belt.List.keep(((loc, message)) => message != ["Error: Uninterpreted extension 'merlin.syntax-error'."]);
      let errors = AsYouType.parseErrors(Utils.splitLines(Utils.stripAnsii(text))) @ errors;
      l(errors |. Belt.List.map(makeDiagnostic(documentText)))
    }
    | Success(text, _) => {
      if (String.trim(text) == "") {
        l([])
      } else {
        let errors = AsYouType.parseErrors(Utils.splitLines(Utils.stripAnsii(text)));
        l(errors |. Belt.List.map(makeDiagnostic(documentText)))
      }
    }
    | TypeError(text, _) => {
      Log.log("type error here " ++ text);
      let errors = AsYouType.parseErrors(Utils.splitLines(Utils.stripAnsii(text)))
      |. Belt.List.keep(((loc, message)) => {
        !Str.string_match(Str.regexp({|.*Missing dependency [a-zA-Z]+ in search path|}), String.concat(" ", message), 0)
      })
      ;
      l(errors |. Belt.List.map(makeDiagnostic(documentText)))
    }
    })
  ]));
};

let checkDocumentTimers = state => {
  let now = Unix.gettimeofday();
  let removed = Hashtbl.fold((uri, timer, removed) => {
    if (now > timer) {
      switch (State.getPackage(uri, state)) {
        | Ok(package) => runDiagnostics(uri, state, ~package);
        | Error(_) => () /* ignore... TODO should I do something */
      };
      [uri, ...removed]
    } else {
      removed
    }
  }, state.documentTimers, []);
  List.iter(uri => Hashtbl.remove(state.documentTimers, uri), removed);
  state
};

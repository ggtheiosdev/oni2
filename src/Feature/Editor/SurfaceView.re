open EditorCoreTypes;
open Revery.UI;
open Revery.Math;

open Oni_Core;

module Log = (val Log.withNamespace("Oni2.Editor.SurfaceView"));

module Config = EditorConfiguration;

module Styles = {
  open Style;

  let bufferViewClipped = (offsetLeft, bufferPixelWidth) => [
    overflow(`Hidden),
    position(`Absolute),
    top(0),
    left(int_of_float(offsetLeft)),
    width(int_of_float(bufferPixelWidth)),
    bottom(0),
  ];
};

module Constants = {
  let hoverTime = 1.0;
};

let drawCurrentLineHighlight = (~context, ~colors: Colors.t, line) =>
  Draw.lineHighlight(~context, ~color=colors.lineHighlightBackground, line);

let renderRulers = (~context: Draw.context, ~colors: Colors.t, rulers) => {
  let characterWidth = Editor.characterWidthInPixels(context.editor);
  let scrollX = Editor.scrollX(context.editor);
  rulers
  |> List.map(pos => characterWidth *. float(pos) -. scrollX)
  |> List.iter(Draw.ruler(~context, ~color=colors.rulerForeground));
};

let%component make =
              (
                ~buffer,
                ~editor,
                ~colors,
                ~dispatch,
                ~topVisibleLine,
                ~onCursorChange,
                ~cursorPosition: Location.t,
                ~editorFont: Service_Font.font,
                ~leftVisibleColumn,
                ~diagnosticsMap,
                ~selectionRanges,
                ~matchingPairs,
                ~bufferHighlights,
                ~languageSupport,
                ~bufferSyntaxHighlights,
                ~bottomVisibleLine,
                ~mode,
                ~isActiveSplit,
                ~gutterWidth,
                ~bufferPixelWidth,
                ~bufferWidthInCharacters,
                ~windowIsFocused,
                ~config,
                (),
              ) => {
  let%hook maybeBbox = React.Hooks.ref(None);
  let%hook hoverTimerActive = React.Hooks.ref(false);
  let%hook lastMousePosition = React.Hooks.ref(None);
  let%hook (hoverTimer, resetHoverTimer) =
    Hooks.timer(~active=hoverTimerActive^, ());

  let lineCount = editor |> Editor.totalViewLines;
  let indentation =
    switch (Buffer.getIndentation(buffer)) {
    | Some(v) => v
    | None => IndentationSettings.default
    };

  let onMouseWheel = (wheelEvent: NodeEvents.mouseWheelEventParams) =>
    dispatch(
      Msg.EditorMouseWheel({
        deltaY: wheelEvent.deltaY *. (-1.),
        deltaX: wheelEvent.deltaX,
      }),
    );

  let getMaybeLocationFromMousePosition = (mouseX, mouseY) => {
    maybeBbox^
    |> Option.map(bbox => {
         let (minX, minY, _, _) = bbox |> BoundingBox2d.getBounds;

         let relX = mouseX -. minX;
         let relY = mouseY -. minY;

         Editor.Slow.pixelPositionToBufferLineByte(
           ~buffer,
           ~pixelX=relX,
           ~pixelY=relY,
           editor,
         );
       });
  };

  let onMouseMove = (evt: NodeEvents.mouseMoveEventParams) => {
    getMaybeLocationFromMousePosition(evt.mouseX, evt.mouseY)
    |> Option.iter(((line, col)) => {
         dispatch(
           Msg.MouseMoved({
             location:
               EditorCoreTypes.Location.create(
                 ~line=Index.fromZeroBased(line),
                 ~column=Index.fromZeroBased(col),
               ),
           }),
         )
       });
    hoverTimerActive := true;
    lastMousePosition := Some((evt.mouseX, evt.mouseY));
    resetHoverTimer();
  };

  let onMouseLeave = _ => {
    hoverTimerActive := false;
    lastMousePosition := None;
    resetHoverTimer();
  };

  // Usually the If hook compares a value to a prior version of itself
  // However, here we just want to compare it to a constant value,
  // so we discard the second argument to the function.
  let%hook () =
    Hooks.effect(
      If(
        (t, _) => Revery.Time.toFloatSeconds(t) > Constants.hoverTime,
        hoverTimer,
      ),
      () => {
        lastMousePosition^
        |> Utility.OptionEx.flatMap(((mouseX, mouseY)) =>
             getMaybeLocationFromMousePosition(mouseX, mouseY)
           )
        |> Option.iter(((line, col)) =>
             dispatch(
               Msg.MouseHovered({
                 location:
                   EditorCoreTypes.Location.create(
                     ~line=Index.fromZeroBased(line),
                     ~column=Index.fromZeroBased(col),
                   ),
               }),
             )
           );
        hoverTimerActive := false;
        resetHoverTimer();
        None;
      },
    );

  let bufferId = buffer |> Oni_Core.Buffer.getId;
  let lenses =
    Feature_LanguageSupport.CodeLens.get(~bufferId, languageSupport);
  let topLine = Editor.getTopVisibleLine(editor);
  let bottomLine = Editor.getBottomVisibleLine(editor);

  let visibleLenses =
    lenses
    |> List.filter(lens => {
         let lensLine = Feature_LanguageSupport.CodeLens.lineNumber(lens);
         lensLine >= topLine && lensLine <= bottomLine;
       });

  let lensElements =
    visibleLenses
    |> List.map(lens => {
         let lineNumber = Feature_LanguageSupport.CodeLens.lineNumber(lens);
         let text = Feature_LanguageSupport.CodeLens.text(lens);

         let ({pixelY, _}: Editor.pixelPosition, _width) =
           Editor.bufferLineByteToPixel(
             ~line=lineNumber,
             ~byteIndex=0,
             editor,
           );

         <View
           style=Style.[
             position(`Absolute),
             top(pixelY |> int_of_float),
             height(20),
             backgroundColor(Revery.Colors.black),
             left(0),
             right(0),
           ]>
           <View style=Style.[flexDirection(`Row), flexGrow(1)]>
             <Text
               text
               style=Style.[
                 color(Revery.Colors.red),
                 flexGrow(1),
                 textOverflow(`Ellipsis),
                 textWrap(Revery.TextWrapping.NoWrap),
               ]
             />
           </View>
         </View>;
       });

  let onMouseUp = (evt: NodeEvents.mouseButtonEventParams) => {
    Log.trace("editorMouseUp");

    getMaybeLocationFromMousePosition(evt.mouseX, evt.mouseY)
    |> Option.iter(((line, col)) => {
         Log.tracef(m => m("  topVisibleLine is %i", topVisibleLine));
         Log.tracef(m => m("  setPosition (%i, %i)", line + 1, col));

         let cursor =
           Vim.Cursor.create(
             ~line=Index.fromOneBased(line + 1),
             ~column=Index.fromZeroBased(col),
           );

         onCursorChange(cursor);
       });
  };

  let pixelWidth = Editor.visiblePixelWidth(editor);
  let pixelHeight = Editor.visiblePixelHeight(editor);

  <View
    onBoundingBoxChanged={bbox => maybeBbox := Some(bbox)}
    style={Styles.bufferViewClipped(
      gutterWidth,
      float(pixelWidth) -. gutterWidth,
    )}
    onMouseUp
    onMouseMove
    onMouseLeave
    onMouseWheel>
    <Canvas
      style={Styles.bufferViewClipped(0., float(pixelWidth) -. gutterWidth)}
      render={(canvasContext, _) => {
        let context =
          Draw.createContext(
            ~canvasContext,
            ~width=pixelWidth,
            ~height=pixelHeight,
            ~editor,
            ~editorFont,
          );

        drawCurrentLineHighlight(
          ~context,
          ~colors,
          cursorPosition.line |> Index.toZeroBased,
        );

        renderRulers(~context, ~colors, Config.rulers.get(config));

        if (Config.renderIndentGuides.get(config)) {
          IndentLineRenderer.render(
            ~context,
            ~buffer,
            ~startLine=topVisibleLine - 1,
            ~endLine=bottomVisibleLine + 1,
            ~cursorPosition,
            ~colors,
            ~showActive=Config.highlightActiveIndentGuide.get(config),
            indentation,
          );
        };

        ContentView.render(
          ~context,
          ~count=lineCount,
          ~buffer,
          ~editor,
          ~leftVisibleColumn,
          ~colors,
          ~diagnosticsMap,
          ~selectionRanges,
          ~matchingPairs,
          ~bufferHighlights,
          ~cursorPosition,
          ~languageSupport,
          ~bufferSyntaxHighlights,
          ~shouldRenderWhitespace=Config.renderWhitespace.get(config),
          ~bufferWidthInCharacters,
          ~bufferWidthInPixels=bufferPixelWidth,
        );

        if (Config.scrollShadow.get(config)) {
          let () =
            ScrollShadow.renderVertical(
              ~editor,
              ~width=float(bufferPixelWidth),
              ~context,
            );
          let () =
            ScrollShadow.renderHorizontal(
              ~editor,
              ~width=float(bufferPixelWidth),
              ~context,
            );
          ();
        };
      }}
    />
    {lensElements |> React.listToElement}
    <CursorView
      config
      editor
      editorFont
      mode
      cursorPosition
      isActiveSplit
      windowIsFocused
      colors
    />
  </View>;
};

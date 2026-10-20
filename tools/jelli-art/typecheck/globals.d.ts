// Page globals the studio scripts share. The inline review script in tools/assets/compare.html
// defines most of them, shell.js defines window.JelliShell, and each module adds its own window
// object. This file only describes them for `tsc --checkJs` (tools/jelli-art/typecheck/check.sh).
export {};

declare global {
  /** One sprite from GET /api/data (tools/assets/compare_slice.py collect()). */
  interface StudioAsset {
    key: string;
    kind: string;
    width: number;
    height: number;
    sha?: string;
    palette?: string[];
    palette_name?: string;
    after_metrics?: { colors: Record<string, number>; specks: number[][]; open_edges: number[][]; opaque?: number };
    before_metrics?: StudioAsset["after_metrics"];
    [field: string]: any;
  }

  /** The GET /api/data payload. */
  interface StudioData {
    assets: StudioAsset[];
    palette: string[];
    version?: string;
    [field: string]: any;
  }

  /** A decoded RGBA image: `data` holds 4 bytes per pixel. */
  interface DecodedImage {
    img: HTMLCanvasElement;
    w: number;
    h: number;
    data: Uint8ClampedArray;
  }

  interface ShellToastOptions {
    /** "info", "ok", "warn" or "bad" ("bad" stays until dismissed). */
    tone?: string;
    sticky?: boolean;
    keep?: boolean;
    hint?: string;
    id?: string;
    /** Milliseconds before a non-sticky toast closes (default 6000). */
    timeout?: number;
  }

  /** window.JelliShell (shell.js); see tools/jelli-art/README.md "Page shell". */
  interface JelliShellApi {
    registerShortcuts(section: string, entries: ({ keys: string[]; description: string } | { gesture: string; description: string })[]): void;
    notify(text: string, options?: ShellToastOptions): string;
    dismiss(id: string): void;
    announce(text: string): void;
    dialog(options: { title: string; body?: string | Node; className?: string; initial?: string; actions?: { label: string; value: any; primary?: boolean; danger?: boolean }[]; onOpen?: (dialog: HTMLDialogElement) => void }): Promise<any>;
    confirm(message: string, options?: { title?: string; confirmLabel?: string; danger?: boolean }): Promise<boolean>;
    registerMode(mode: { id: string; label: string; view?: string; [field: string]: any }): void;
    registerDirty(id: string, entry: { label: string; mode: string | null; check: () => number | boolean }): void;
    registerViewRoot(id: string): void;
    showOnly(id: string): void;
    dirtyItems(): { id: string; n: number; label: string; mode: string | null; [field: string]: any }[];
    activateMode(id: string): void;
    openHelp(): void;
    setTheme(theme: "auto" | "dark" | "light" | "contrast"): void;
    sync(): void;
    showGuide(show: boolean): void;
    addSetting(node: Node): HTMLElement;
    openSettings(): void;
    shortcuts(): Map<string, any>;
  }

  /** A gesture snapshot: scale since the gesture began, midpoint now (cx, cy) and at the start (x0, y0), client px. */
  interface TouchGesture { scale: number; cx: number; cy: number; x0: number; y0: number }

  /** window.JelliTouch (touch.js, RFC-007); see tools/jelli-art/README.md "Touch and Pencil". */
  interface JelliTouchApi {
    T: Readonly<{ SECOND_FINGER_MS: number; PALM_MS: number; PAN_PX: number; PINCH_RATIO: number; TAP_DOWN_MS: number;
      TAP_UP_MS: number; TAP_MOVE_PX: number; LONG_PRESS_MS: number; LONG_PRESS_MOVE_PX: number }>;
    kind(e: PointerEvent): "pen" | "touch" | "mouse";
    coarse(): boolean;
    pencilOnly(): boolean;
    setPencilOnly(on: boolean): void;
    onPencilOnly(fn: (on: boolean) => void): () => void;
    offerPencilOnly(): void;
    gestures(handlers: {
      stroke?: { start?(e: PointerEvent): void; move?(e: PointerEvent): void; end?(e: PointerEvent): void; cancel?(e: PointerEvent): void };
      pan?(dx: number, dy: number): void; pinch?(g: TouchGesture): void; gestureEnd?(g: TouchGesture): void;
      tap?(fingers: number, g: TouchGesture): void; point?(e: PointerEvent): void; longPress?(e: PointerEvent): void;
    }, options?: { pencilOnly?: () => boolean; offerPencilOnly?: boolean }): {
      handle(e: PointerEvent): "stroke" | "gesture" | "ignored"; attach(el: HTMLElement): () => void; reset(): void; busy(): boolean;
    };
    registerGestures(list: { gesture: string; description: string }[]): void;
    info(text: string, label?: string): string;
    popover(text: string, anchor: Element): void;
    hidePopover(): void;
    setList(open: boolean): void;
  }

  /** localStorage wrapper (prefix "jelli-review:"); reads fall back to `fallback`. */
  const store: { get<T = any>(key: string, fallback?: T): T; set(key: string, value: any): void };
  /** Review and paint state (selected asset, view, mode, zoom, tool, colours, ...). */
  const state: { key: string | null; view: string; mode: string; zoom: number | null; [field: string]: any };
  let D: StudioData;
  let byKey: Record<string, StudioAsset>;
  let decoded: Record<string, { before: DecodedImage | null; after: DecodedImage | null }>;
  const DPR: number;
  const MODES: string[];

  function asset(): StudioAsset;
  function pixel(image: DecodedImage | null, x: number, y: number): string | null;
  function label(hex: string, a: StudioAsset): string;
  function slotLabel(index: number): string;
  function backdropFor(a: StudioAsset): string;
  function backdropLuma(name: string): number;
  function makeCanvas(cssWidth: number, cssHeight: number): [HTMLCanvasElement, CanvasRenderingContext2D];
  function fillBackdrop(ctx: CanvasRenderingContext2D, w: number, h: number, z: number, name: string): void;
  function drawPixels(ctx: CanvasRenderingContext2D, image: DecodedImage | null, z: number, options?: object): void;
  function hexOf(data: Uint8ClampedArray, index: number): string;
  function lumaOf(hex: string): number;
  function fitZoom(a: StudioAsset): number;
  function loadPayload(data: StudioData, beforePublish?: () => boolean | void): Promise<boolean>;
  function drawGrid(ctx: CanvasRenderingContext2D, w: number, h: number, z: number): void;
  function drawIssues(ctx: CanvasRenderingContext2D, metrics: StudioAsset["after_metrics"], z: number): void;
  function drawHover(ctx: CanvasRenderingContext2D, z: number): void;
  function savePrefs(): void;
  function setZoom(z: number | null): void;
  // compare.html render functions; modules wrap some of them by reassignment.
  let select: (key: string) => void;
  let renderView: () => void;
  function renderStage(): void;
  function renderList(): void;
  function renderTotals(): void;
  function renderHeader(a: StudioAsset): void;
  function renderInspector(a: StudioAsset): void;
  function renderPalette(a: StudioAsset): void;
  function renderKinds(): void;
  function renderContext(a: StudioAsset): void;

  // Module objects (window.X in the browser).
  const JelliShell: JelliShellApi;
  const JelliTouch: JelliTouchApi;
  const JelliDrafts: any;
  const JelliPaint: any;
  const JelliLint: any;
  const JelliAnimation: any;
  const Studio: any;
  /** paint_tools.js and lint.js also load under node (module.exports). */
  const module: { exports: any } | undefined;

  interface Window {
    JelliIcons: { svg(name: string): string };
    JelliShell: JelliShellApi;
    JelliTouch: JelliTouchApi;
    JelliDrafts: any;
    JelliPaint: any;
    JelliLint: any;
    JelliAnimation: any;
    Studio: any;
  }
}

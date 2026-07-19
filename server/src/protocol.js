export function sanitizePlayerName(value) {
  return [...String(value || "").trim()]
    .filter((character) => /[A-Za-z0-9 _-]/.test(character))
    .slice(0, 16)
    .join("");
}

export function decodeClientMessage(message) {
  let data;
  try {
    data = JSON.parse(
      typeof message === "string"
        ? message
        : new TextDecoder().decode(message),
    );
  } catch {
    return { ok: false, error: "invalid_json" };
  }
  if (!data || typeof data !== "object" || Array.isArray(data))
    return { ok: false, error: "invalid_message" };
  return { ok: true, data };
}

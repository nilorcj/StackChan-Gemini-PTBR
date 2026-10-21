export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // ==================================================
    // ENDPOINT EXPERIMENTAL: POST /v2/echo (HMAC-SHA256)
    // ==================================================
    if (request.method === "POST" && url.pathname === "/v2/echo") {
      if (!env.HMAC_SHARED_KEY) {
        return jsonResponse(
          { ok: false, error: "server_not_configured" },
          500
        );
      }

      const timestamp = request.headers.get("X-Timestamp");
      const nonce = request.headers.get("X-Nonce");
      const signature = request.headers.get("X-Signature");

      if (!timestamp || !/^\d+$/.test(timestamp.trim())) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      const ts = Number(timestamp.trim());
      if (!Number.isFinite(ts) || ts <= 0) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      const now = Math.floor(Date.now() / 1000);
      if (Math.abs(now - ts) > 120) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      if (!nonce || !nonce.trim()) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      if (!signature || !/^[0-9a-fA-F]+$/.test(signature.trim())) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      const rawBody = await request.text();

      const canonicalString =
        "POST\n" +
        "/v2/echo\n" +
        timestamp + "\n" +
        nonce + "\n" +
        rawBody;

      const expectedSignature = await computeHmacSha256Hex(
        env.HMAC_SHARED_KEY,
        canonicalString
      );

      if (!timingSafeEqualHex(signature, expectedSignature)) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      let parsedBody;
      try {
        parsedBody = JSON.parse(rawBody);
      } catch {
        return jsonResponse({ ok: false, error: "invalid_json" }, 400);
      }

      return jsonResponse({
        ok: true,
        endpoint: "v2/echo",
        echo: parsedBody
      });
    }

    let body;

    // ==================================================
    // ENDPOINT HMAC-ONLY: POST /v2/call
    // ==================================================
    if (request.method === "POST" && url.pathname === "/v2/call") {
      if (request.headers.get("Authorization") !== null) {
        return jsonResponse(
          { ok: false, error: "authorization_not_allowed" },
          400
        );
      }

      if (!env.HMAC_SHARED_KEY) {
        return jsonResponse(
          { ok: false, error: "server_not_configured" },
          500
        );
      }

      const contentLengthHeader = request.headers.get("Content-Length");
      if (contentLengthHeader !== null) {
        const contentLength = Number(contentLengthHeader.trim());
        if (!Number.isFinite(contentLength) || contentLength < 0) {
          return jsonResponse({ ok: false, error: "invalid_content_length" }, 400);
        }
        if (contentLength > 16384) {
          return jsonResponse({ ok: false, error: "payload_too_large" }, 413);
        }
      }

      const timestamp = request.headers.get("X-Timestamp");
      const nonce = request.headers.get("X-Nonce");
      const signature = request.headers.get("X-Signature");

      if (
        !timestamp || !/^\d{10}$/.test(timestamp) ||
        !nonce || !/^[0-9a-f]{32}$/.test(nonce) ||
        !signature || !/^[0-9a-f]{64}$/.test(signature)
      ) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      const ts = Number(timestamp);
      const nowSeconds = Math.floor(Date.now() / 1000);
      if (!Number.isInteger(ts) || Math.abs(nowSeconds - ts) > 120) {
        return jsonResponse({ ok: false, error: "timestamp_skew" }, 401);
      }

      const rawBody = await request.text();
      const rawBodyBytes = new TextEncoder().encode(rawBody).byteLength;
      if (rawBodyBytes > 16384) {
        return jsonResponse({ ok: false, error: "payload_too_large" }, 413);
      }

      const canonicalString =
        "POST\n" +
        url.pathname + "\n" +
        timestamp + "\n" +
        nonce + "\n" +
        rawBody;

      const expectedSignature = await computeHmacSha256Hex(
        env.HMAC_SHARED_KEY,
        canonicalString
      );

      if (!timingSafeEqualHex(signature, expectedSignature)) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      try {
        body = JSON.parse(rawBody);
      } catch {
        return jsonResponse(
          { ok: false, error: "invalid_json" },
          400
        );
      }

      if (!body || typeof body !== "object" || Array.isArray(body)) {
        return jsonResponse(
          { ok: false, error: "invalid_json" },
          400
        );
      }
    } else {
      return jsonResponse(
        { ok: false, error: "not_found" },
        404
      );
    }

    const tool = body.tool || body.tool_name;
    const args = body.arguments || {};

    // ==================================================
    // TOOL 1: WEB SEARCH
    // TinyFish
    // ==================================================

    if (tool === "web_search") {
      const query = String(args.query || "").trim();

      if (!query) {
        return jsonResponse(
          { ok: false, error: "missing_query" },
          400
        );
      }

      try {
        const tinyfishUrl =
          `https://api.search.tinyfish.ai?query=${encodeURIComponent(query)}`;

        const tinyfishResponse = await fetch(
          tinyfishUrl,
          {
            method: "GET",
            headers: {
              "X-API-Key": env.TINYFISH_API_KEY
            }
          }
        );

        const text = await tinyfishResponse.text();

        if (!tinyfishResponse.ok) {
          return jsonResponse(
            {
              ok: false,
              error: "tinyfish_error",
              status: tinyfishResponse.status
            },
            502
          );
        }

        let data;

        try {
          data = JSON.parse(text);
        } catch {
          return jsonResponse(
            {
              ok: false,
              error: "invalid_tinyfish_json"
            },
            502
          );
        }

        const results = Array.isArray(data.results)
          ? data.results.slice(0, 5).map(item => ({
              title: item.title || "",
              url: item.url || "",
              date: item.date || "",
              site: item.site_name || "",
              snippet: shorten(item.snippet || "", 500)
            }))
          : [];

        return jsonResponse({
          ok: true,
          tool: "web_search",
          provider: "tinyfish",
          query,
          results
        });

      } catch (error) {
        return jsonResponse(
          {
            ok: false,
            error: "tinyfish_fetch_failed",
            details: String(error)
          },
          502
        );
      }
    }

    // ==================================================
    // TOOL 2: GET AGENDA
    // Google Calendar via ICS privado
    // ==================================================

    if (tool === "get_agenda") {
      if (!env.CALENDAR_ICS_URL) {
        return jsonResponse(
          {
            ok: false,
            error: "calendar_ics_url_not_configured"
          },
          500
        );
      }

      try {
        const response = await fetch(
          env.CALENDAR_ICS_URL,
          {
            method: "GET",
            headers: {
              "User-Agent": "Pix-ToolGateway/1.0"
            }
          }
        );

        if (!response.ok) {
          return jsonResponse(
            {
              ok: false,
              error: "calendar_fetch_failed",
              status: response.status
            },
            502
          );
        }

        const ics = await response.text();

        const rawPeriod = String(args.period || "upcoming").trim().toLowerCase();
        const period =
          (rawPeriod === "today" || rawPeriod === "hoje") ? "today" :
          (rawPeriod === "tomorrow" || rawPeriod === "amanha" || rawPeriod === "amanhã") ? "tomorrow" :
          "upcoming";

        let limit = Number(args.limit || 20);
        if (!Number.isFinite(limit)) {
          limit = 20;
        }
        limit = Math.max(1, Math.min(Math.trunc(limit), 20));

        const now = new Date();
        const agenda = buildAgendaEvents(ics, now, period, limit);

        return jsonResponse({
          ok: true,
          tool: "get_agenda",
          provider: "google_calendar_ics",
          period,
          timezone: "America/Sao_Paulo",
          today: agenda.today,
          tomorrow: agenda.tomorrow,
          count: agenda.events.length,
          events: agenda.events
        });

      } catch (error) {
        return jsonResponse(
          {
            ok: false,
            error: "calendar_processing_failed",
            details: String(error)
          },
          502
        );
      }
    }

    return jsonResponse(
      {
        ok: false,
        error: "unknown_tool"
      },
      404
    );
  }
};


// ==================================================
// FUNÇÕES AUXILIARES HMAC
// ==================================================

async function computeHmacSha256Hex(secret, message) {
  const enc = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    enc.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );
  const signatureBuffer = await crypto.subtle.sign(
    "HMAC",
    key,
    enc.encode(message)
  );
  const hashArray = Array.from(new Uint8Array(signatureBuffer));
  return hashArray.map(b => b.toString(16).padStart(2, "0")).join("");
}

function timingSafeEqualHex(a, b) {
  if (typeof a !== "string" || typeof b !== "string") {
    return false;
  }

  const strA = a.trim().toLowerCase();
  const strB = b.trim().toLowerCase();

  let diff = (strA.length === 64 && strB.length === 64) ? 0 : 1;

  for (let i = 0; i < 64; i++) {
    const charA = strA.charCodeAt(i % (strA.length || 1)) || 0;
    const charB = strB.charCodeAt(i % (strB.length || 1)) || 0;
    diff |= charA ^ charB;
  }

  return diff === 0;
}


// ==================================================
// FUNÇÕES AUXILIARES DE AGENDA / ICS (AMERICA/SAO_PAULO)
// ==================================================

const BRASILIA_FORMATTER = new Intl.DateTimeFormat(
  "en-CA",
  {
    timeZone: "America/Sao_Paulo",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    hourCycle: "h23"
  }
);

const WEEKDAY_CODES = ["SU", "MO", "TU", "WE", "TH", "FR", "SA"];

function getBrasiliaParts(date) {
  const values = {};
  for (const part of BRASILIA_FORMATTER.formatToParts(date)) {
    if (part.type !== "literal") {
      values[part.type] = part.value;
    }
  }
  return values;
}

function makeTargetDay(baseYear, baseMonth, baseDay, dayOffset) {
  const utcMs = Date.UTC(baseYear, baseMonth - 1, baseDay + dayOffset);
  const d = new Date(utcMs);
  const year = d.getUTCFullYear();
  const month = d.getUTCMonth() + 1;
  const day = d.getUTCDate();
  const wday = d.getUTCDay();
  const dateStr = `${year}-${pad(month)}-${pad(day)}`;
  const ymd = `${year}${pad(month)}${pad(day)}`;
  return {
    dayOffset,
    year,
    month,
    day,
    wday,
    dayCode: WEEKDAY_CODES[wday],
    utcMidnightMs: utcMs,
    dateStr,
    ymd
  };
}

function daysInMonthUTC(year, month) {
  return new Date(Date.UTC(year, month, 0)).getUTCDate();
}

function buildAgendaEvents(icsText, nowDate, period, limit) {
  const nowParts = getBrasiliaParts(nowDate);
  const baseYear = Number(nowParts.year);
  const baseMonth = Number(nowParts.month);
  const baseDay = Number(nowParts.day);

  const todayTarget = makeTargetDay(baseYear, baseMonth, baseDay, 0);
  const tomorrowTarget = makeTargetDay(baseYear, baseMonth, baseDay, 1);
  const yesterdayTarget = makeTargetDay(baseYear, baseMonth, baseDay, -1);

  let targetDays;
  if (period === "today") {
    targetDays = [todayTarget];
  } else if (period === "tomorrow") {
    targetDays = [tomorrowTarget];
  } else {
    targetDays = [];
    for (let d = 0; d <= 30; d++) {
      targetDays.push(makeTargetDay(baseYear, baseMonth, baseDay, d));
    }
  }

  const minRawYmd = yesterdayTarget.ymd;
  const todayYmd = todayTarget.ymd;
  const recentCutoffYmd = makeTargetDay(baseYear, baseMonth, baseDay, -45).ymd;
  const oldCountCutoffYmd = makeTargetDay(baseYear, baseMonth, baseDay, -120).ymd;
  const curMonthPadded = pad(baseMonth);
  const nextMonthPadded = pad(baseMonth === 12 ? 1 : baseMonth + 1);

  // Passo 1: Varredura rápida sem regex global nem split no arquivo inteiro
  const candidateBlocks = [];
  const overriddenSet = new Set();

  let cursor = 0;
  while (true) {
    const startIdx = icsText.indexOf("BEGIN:VEVENT", cursor);
    if (startIdx === -1) break;
    const endIdx = icsText.indexOf("END:VEVENT", startIdx + 12);
    if (endIdx === -1) break;
    cursor = endIdx + 10;

    const dtIdx = icsText.indexOf("DTSTART", startIdx + 12);
    if (dtIdx === -1 || dtIdx >= endIdx) continue;
    const colonIdx = icsText.indexOf(":", dtIdx + 7);
    if (colonIdx === -1 || colonIdx >= endIdx) continue;
    const startYmd = icsText.substring(colonIdx + 1, colonIdx + 9);

    const rawBlock = icsText.substring(startIdx + 12, endIdx);

    if (startYmd < minRawYmd) {
      const rruleIdx = rawBlock.indexOf("RRULE");
      if (rruleIdx === -1) {
        if (startYmd < recentCutoffYmd) continue;
        const recIdx = rawBlock.indexOf("RECURRENCE-ID");
        if (recIdx === -1) {
          const endPropIdx = rawBlock.indexOf("DTEND");
          if (endPropIdx === -1) continue;
          const endColonIdx = rawBlock.indexOf(":", endPropIdx + 5);
          if (endColonIdx === -1 || rawBlock.substring(endColonIdx + 1, endColonIdx + 9) <= todayYmd) {
            continue;
          }
        }
      } else {
        if (rawBlock.indexOf("FREQ=YEARLY", rruleIdx) !== -1) {
          const evMonth = startYmd.substring(4, 6);
          if (evMonth !== curMonthPadded && evMonth !== nextMonthPadded) {
            continue;
          }
        }
        const untilIdx = rawBlock.indexOf("UNTIL=", rruleIdx);
        if (untilIdx !== -1) {
          const untilYmd = rawBlock.substring(untilIdx + 6, untilIdx + 14);
          if (/^\d{8}$/.test(untilYmd) && untilYmd < minRawYmd) {
            continue;
          }
        }
        if (startYmd < oldCountCutoffYmd && rawBlock.indexOf("COUNT=", rruleIdx) !== -1) {
          if (rawBlock.indexOf("FREQ=DAILY", rruleIdx) !== -1 || rawBlock.indexOf("FREQ=WEEKLY", rruleIdx) !== -1) {
            continue;
          }
        }
      }
    }

    const parsedBlock = parseCandidateBlock(rawBlock);
    if (!parsedBlock) continue;

    if (parsedBlock.recurrenceIdDate) {
      if (parsedBlock.uid) {
        overriddenSet.add(`${parsedBlock.uid}|${parsedBlock.recurrenceIdDate}`);
      }
      if (parsedBlock.status === "CANCELLED") {
        continue;
      }
    } else if (parsedBlock.status === "CANCELLED") {
      continue;
    }

    candidateBlocks.push(parsedBlock);
  }

  // Passo 2: Gerar ocorrências apenas na janela solicitada
  const occurrences = [];

  for (const ev of candidateBlocks) {
    if (!ev.rrule) {
      if (ev.allDay && ev.end && ev.end.date > ev.start.date) {
        for (const tgt of targetDays) {
          if (tgt.dateStr >= ev.start.date && tgt.dateStr < ev.end.date) {
            occurrences.push(makeOccurrence(ev, tgt.dateStr, todayTarget.dateStr, tomorrowTarget.dateStr));
          }
        }
      } else {
        if (period === "today") {
          if (ev.start.date === todayTarget.dateStr) {
            occurrences.push(makeOccurrence(ev, ev.start.date, todayTarget.dateStr, tomorrowTarget.dateStr));
          }
        } else if (period === "tomorrow") {
          if (ev.start.date === tomorrowTarget.dateStr) {
            occurrences.push(makeOccurrence(ev, ev.start.date, todayTarget.dateStr, tomorrowTarget.dateStr));
          }
        } else {
          if (ev.start.date >= todayTarget.dateStr) {
            occurrences.push(makeOccurrence(ev, ev.start.date, todayTarget.dateStr, tomorrowTarget.dateStr));
          }
        }
      }
      continue;
    }

    // Evento recorrente (RRULE): avaliar matematicamente nos dias-alvo
    let dailyAddedInUpcoming = 0;
    for (const tgt of targetDays) {
      if (ev.uid && overriddenSet.has(`${ev.uid}|${tgt.dateStr}`)) {
        continue;
      }
      if (ev.exdates && ev.exdates.has(tgt.dateStr)) {
        continue;
      }
      if (matchesRruleOnTargetDay(ev.start, ev.rrule, tgt)) {
        if (period === "upcoming" && ev.rrule.freq === "DAILY") {
          if (tgt.dayOffset >= 2 && dailyAddedInUpcoming >= 2) {
            continue;
          }
          dailyAddedInUpcoming++;
        }
        occurrences.push(makeOccurrence(ev, tgt.dateStr, todayTarget.dateStr, tomorrowTarget.dateStr));
      }
    }
  }

  occurrences.sort((a, b) => {
    if (a.date !== b.date) {
      return a.date < b.date ? -1 : 1;
    }
    if (a.all_day !== b.all_day) {
      return a.all_day ? -1 : 1;
    }
    if (a.time !== b.time) {
      return a.time < b.time ? -1 : 1;
    }
    return a.description < b.description ? -1 : (a.description > b.description ? 1 : 0);
  });

  return {
    today: todayTarget.dateStr,
    tomorrow: tomorrowTarget.dateStr,
    events: occurrences.slice(0, limit)
  };
}

function makeOccurrence(ev, dateStr, todayStr, tomorrowStr) {
  const dayLabel =
    dateStr === todayStr ? "today" :
    dateStr === tomorrowStr ? "tomorrow" :
    dateStr;

  return {
    date: dateStr,
    time: ev.start.time,
    description: ev.description,
    location: ev.location,
    all_day: ev.allDay,
    day: dayLabel
  };
}

function parseCandidateBlock(rawBlock) {
  // Remover blocos VALARM internos para não capturar SUMMARY/DESCRIPTION de alarmes
  let cleanBlock = rawBlock;
  while (true) {
    const alarmStart = cleanBlock.indexOf("BEGIN:VALARM");
    if (alarmStart === -1) break;
    const alarmEnd = cleanBlock.indexOf("END:VALARM", alarmStart + 12);
    if (alarmEnd === -1) {
      cleanBlock = cleanBlock.substring(0, alarmStart);
      break;
    }
    cleanBlock = cleanBlock.substring(0, alarmStart) + cleanBlock.substring(alarmEnd + 10);
  }

  const rawLines = cleanBlock.split("\n");
  const lines = [];
  let skipContinuation = true;

  for (let i = 0; i < rawLines.length; i++) {
    let rawLine = rawLines[i];
    if (rawLine.endsWith("\r")) {
      rawLine = rawLine.substring(0, rawLine.length - 1);
    }
    if (!rawLine) continue;
    const firstChar = rawLine.charCodeAt(0);
    if (firstChar === 32 || firstChar === 9) {
      if (!skipContinuation && lines.length > 0) {
        lines[lines.length - 1] += rawLine.substring(1);
      }
      continue;
    }
    if (
      rawLine.startsWith("SUMMARY:") || rawLine.startsWith("SUMMARY;") ||
      rawLine.startsWith("LOCATION:") || rawLine.startsWith("LOCATION;") ||
      rawLine.startsWith("DTSTART:") || rawLine.startsWith("DTSTART;") ||
      rawLine.startsWith("DTEND:") || rawLine.startsWith("DTEND;") ||
      rawLine.startsWith("RRULE:") || rawLine.startsWith("RRULE;") ||
      rawLine.startsWith("EXDATE:") || rawLine.startsWith("EXDATE;") ||
      rawLine.startsWith("RECURRENCE-ID:") || rawLine.startsWith("RECURRENCE-ID;") ||
      rawLine.startsWith("STATUS:") || rawLine.startsWith("STATUS;") ||
      rawLine.startsWith("UID:") || rawLine.startsWith("UID;")
    ) {
      lines.push(rawLine);
      skipContinuation = false;
    } else {
      skipContinuation = true;
    }
  }

  let uid = "";
  let summary = "";
  let location = "";
  let status = "CONFIRMED";
  let dtstartProp = null;
  let dtendProp = null;
  let rruleRaw = "";
  let recurrenceIdProp = null;
  let exdates = null;

  for (const line of lines) {
    if (line.startsWith("SUMMARY:") || line.startsWith("SUMMARY;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) summary = line.substring(idx + 1);
    } else if (line.startsWith("LOCATION:") || line.startsWith("LOCATION;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) location = line.substring(idx + 1);
    } else if (line.startsWith("DTSTART:") || line.startsWith("DTSTART;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) {
        dtstartProp = { parameters: line.substring(0, idx), value: line.substring(idx + 1).trim() };
      }
    } else if (line.startsWith("DTEND:") || line.startsWith("DTEND;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) {
        dtendProp = { parameters: line.substring(0, idx), value: line.substring(idx + 1).trim() };
      }
    } else if (line.startsWith("RRULE:") || line.startsWith("RRULE;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) rruleRaw = line.substring(idx + 1).trim();
    } else if (line.startsWith("EXDATE:") || line.startsWith("EXDATE;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) {
        const params = line.substring(0, idx);
        const vals = line.substring(idx + 1).trim().split(",");
        if (!exdates) exdates = new Set();
        for (const v of vals) {
          const parsedEx = parseICSDate(v.trim(), params);
          if (parsedEx) exdates.add(parsedEx.date);
        }
      }
    } else if (line.startsWith("RECURRENCE-ID:") || line.startsWith("RECURRENCE-ID;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) {
        recurrenceIdProp = { parameters: line.substring(0, idx), value: line.substring(idx + 1).trim() };
      }
    } else if (line.startsWith("STATUS:") || line.startsWith("STATUS;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) status = line.substring(idx + 1).trim().toUpperCase();
    } else if (line.startsWith("UID:") || line.startsWith("UID;")) {
      const idx = line.indexOf(":");
      if (idx !== -1) uid = line.substring(idx + 1).trim();
    }
  }

  if (!dtstartProp) {
    return null;
  }

  const start = parseICSDate(dtstartProp.value, dtstartProp.parameters);
  if (!start) {
    return null;
  }

  const end = dtendProp ? parseICSDate(dtendProp.value, dtendProp.parameters) : null;
  const recurrenceIdParsed = recurrenceIdProp
    ? parseICSDate(recurrenceIdProp.value, recurrenceIdProp.parameters)
    : null;

  return {
    uid,
    status,
    start,
    end,
    allDay: start.allDay,
    description: cleanICSText(summary),
    location: cleanICSText(location),
    rrule: rruleRaw ? parseRrule(rruleRaw) : null,
    exdates,
    recurrenceIdDate: recurrenceIdParsed ? recurrenceIdParsed.date : ""
  };
}

function parseICSDate(value, parameters) {
  if (!value) {
    return null;
  }

  if (/^\d{8}$/.test(value)) {
    const year = Number(value.substring(0, 4));
    const month = Number(value.substring(4, 6));
    const day = Number(value.substring(6, 8));
    const utcMidnightMs = Date.UTC(year, month - 1, day);
    const wday = new Date(utcMidnightMs).getUTCDay();

    return {
      year,
      month,
      day,
      hour: 0,
      minute: 0,
      wday,
      utcMidnightMs,
      allDay: true,
      date: `${year}-${pad(month)}-${pad(day)}`,
      time: ""
    };
  }

  const match = value.match(
    /^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})?(Z)?$/
  );

  if (!match) {
    return null;
  }

  let year = Number(match[1]);
  let month = Number(match[2]);
  let day = Number(match[3]);
  let hour = Number(match[4]);
  let minute = Number(match[5]);
  const second = Number(match[6] || 0);
  const isUTC = Boolean(match[7]);

  if (isUTC) {
    // Conversão rápida UTC -> America/Sao_Paulo (UTC-3) sem instanciar Intl.DateTimeFormat
    const brDate = new Date(Date.UTC(year, month - 1, day, hour - 3, minute, second));
    year = brDate.getUTCFullYear();
    month = brDate.getUTCMonth() + 1;
    day = brDate.getUTCDate();
    hour = brDate.getUTCHours();
    minute = brDate.getUTCMinutes();
  }

  const utcMidnightMs = Date.UTC(year, month - 1, day);
  const wday = new Date(utcMidnightMs).getUTCDay();

  return {
    year,
    month,
    day,
    hour,
    minute,
    wday,
    utcMidnightMs,
    allDay: false,
    date: `${year}-${pad(month)}-${pad(day)}`,
    time: `${pad(hour)}:${pad(minute)}`
  };
}

function parseRrule(rruleString) {
  const parts = rruleString.split(";");
  const rule = {
    freq: "",
    interval: 1,
    count: 0,
    untilDate: "",
    byday: [],
    bymonthday: []
  };

  for (const part of parts) {
    const eqIdx = part.indexOf("=");
    if (eqIdx === -1) continue;
    const key = part.substring(0, eqIdx).trim().toUpperCase();
    const val = part.substring(eqIdx + 1).trim().toUpperCase();

    if (key === "FREQ") {
      rule.freq = val;
    } else if (key === "INTERVAL") {
      const n = Number(val);
      if (Number.isFinite(n) && n >= 1) rule.interval = Math.trunc(n);
    } else if (key === "COUNT") {
      const n = Number(val);
      if (Number.isFinite(n) && n >= 1) rule.count = Math.trunc(n);
    } else if (key === "UNTIL") {
      const parsedUntil = parseICSDate(val, "");
      if (parsedUntil) {
        rule.untilDate = parsedUntil.date;
      }
    } else if (key === "BYDAY") {
      rule.byday = val.split(",").map(s => s.trim()).filter(Boolean);
    } else if (key === "BYMONTHDAY") {
      rule.bymonthday = val
        .split(",")
        .map(s => Number(s.trim()))
        .filter(n => Number.isFinite(n) && n !== 0);
    }
  }

  return rule.freq ? rule : null;
}

function matchesRruleOnTargetDay(start, rrule, tgt) {
  const diffDays = Math.round((tgt.utcMidnightMs - start.utcMidnightMs) / 86400000);
  if (diffDays < 0) {
    return false;
  }

  if (rrule.untilDate && tgt.dateStr > rrule.untilDate) {
    return false;
  }

  const interval = rrule.interval || 1;

  if (rrule.freq === "DAILY") {
    if (diffDays % interval !== 0) return false;
    const occIndex = Math.floor(diffDays / interval) + 1;
    if (rrule.count > 0 && occIndex > rrule.count) return false;
    return true;
  }

  if (rrule.freq === "WEEKLY") {
    const dayTokens = rrule.byday.length > 0
      ? rrule.byday.map(tok => tok.slice(-2))
      : [WEEKDAY_CODES[start.wday]];

    if (!dayTokens.includes(tgt.dayCode)) {
      return false;
    }

    const alignedDiffDays = diffDays + start.wday - tgt.wday;
    const diffWeeks = Math.round(alignedDiffDays / 7);
    if (diffWeeks < 0 || diffWeeks % interval !== 0) {
      return false;
    }

    if (rrule.count > 0) {
      const cycleIndex = Math.floor(diffWeeks / interval);
      const validDayNums = dayTokens
        .map(code => WEEKDAY_CODES.indexOf(code))
        .filter(n => n >= 0)
        .sort((a, b) => a - b);

      let occIndex = 0;
      if (cycleIndex === 0) {
        for (const dNum of validDayNums) {
          if (dNum >= start.wday && dNum <= tgt.wday) occIndex++;
        }
      } else {
        let firstWeekCount = 0;
        for (const dNum of validDayNums) {
          if (dNum >= start.wday) firstWeekCount++;
        }
        let targetWeekRank = 0;
        for (const dNum of validDayNums) {
          if (dNum <= tgt.wday) targetWeekRank++;
        }
        occIndex = firstWeekCount + (cycleIndex - 1) * validDayNums.length + targetWeekRank;
      }

      if (occIndex > rrule.count) return false;
    }

    return true;
  }

  if (rrule.freq === "MONTHLY") {
    const diffMonths = (tgt.year - start.year) * 12 + (tgt.month - start.month);
    if (diffMonths < 0 || diffMonths % interval !== 0) {
      return false;
    }

    const occIndex = Math.floor(diffMonths / interval) + 1;
    if (rrule.count > 0 && occIndex > rrule.count) {
      return false;
    }

    if (rrule.bymonthday.length > 0) {
      return rrule.bymonthday.includes(tgt.day);
    }

    if (rrule.byday.length > 0) {
      const totalDays = daysInMonthUTC(tgt.year, tgt.month);
      for (const token of rrule.byday) {
        const m = token.match(/^([+-]?\d+)?([A-Z]{2})$/);
        if (!m) continue;
        const ord = m[1] ? Number(m[1]) : 0;
        const code = m[2];
        if (code !== tgt.dayCode) continue;
        if (ord === 0) return true;
        if (ord > 0) {
          const nthFromStart = Math.floor((tgt.day - 1) / 7) + 1;
          if (nthFromStart === ord) return true;
        } else {
          const nthFromEnd = Math.floor((totalDays - tgt.day) / 7) + 1;
          if (nthFromEnd === -ord) return true;
        }
      }
      return false;
    }

    return tgt.day === start.day;
  }

  if (rrule.freq === "YEARLY") {
    const diffYears = tgt.year - start.year;
    if (diffYears < 0 || diffYears % interval !== 0) {
      return false;
    }
    if (tgt.month !== start.month || tgt.day !== start.day) {
      return false;
    }
    const occIndex = Math.floor(diffYears / interval) + 1;
    if (rrule.count > 0 && occIndex > rrule.count) {
      return false;
    }
    return true;
  }

  return false;
}


function cleanICSText(text) {
  if (!text) {
    return "";
  }

  return text
    .replace(/\\n/gi, " ")
    .replace(/\\,/g, ",")
    .replace(/\\;/g, ";")
    .replace(/\\\\/g, "\\")
    .trim();
}


function shorten(text, maxLength) {
  text = String(text || "")
    .replace(/\s+/g, " ")
    .trim();

  if (text.length <= maxLength) {
    return text;
  }

  return text.substring(0, maxLength) + "...";
}


function pad(value) {
  return String(value).padStart(2, "0");
}


function jsonResponse(data, status = 200) {
  return new Response(
    JSON.stringify(data),
    {
      status,
      headers: {
        "Content-Type":
          "application/json; charset=utf-8"
      }
    }
  );
}


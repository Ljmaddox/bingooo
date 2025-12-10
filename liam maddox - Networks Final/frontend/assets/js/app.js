const API_BASE = "/api";

let currentUser = { id: null, username: "Guest" };
let boardState = null;
let clickLocked = false;
let highlightedBingoSquares = new Set();

let alreadyBingoPopupShown = false;

// refs
const boardEl = document.getElementById('board');
const statusEl = document.getElementById('status');
const usernameEl = document.getElementById('username');
const popup = document.getElementById('popup');

const loginBtn = document.getElementById('login');
const newBoardBtn = document.getElementById('new-board');
const resetBoardBtn = document.getElementById('reset-board');
const openLeaderboardBtn = document.getElementById('open-leaderboard');

const modal = document.getElementById('modal');
const modalUsername = document.getElementById('modal-username');
const modalConfirm = document.getElementById('modal-confirm');
const modalCancel = document.getElementById('modal-cancel');

const leaderboardPopup = document.getElementById('leaderboard-popup');
const leaderboardEl = document.getElementById('leaderboard');
const closeLeaderboardBtn = document.getElementById('close-leaderboard');

const bingoConfirmPopup = document.getElementById('bingo-confirm-popup');
const bingoConfirmYes = document.getElementById('bingo-confirm-yes');
const bingoConfirmNo = document.getElementById('bingo-confirm-no');

const leaderboardSSE = startLeaderboardListener();

fetchLeaderboardOnce();

//api

async function fetchBoard() {
  highlightedBingoSquares = new Set();
  alreadyBingoPopupShown = false;
  if (!currentUser.id) {
    loginBtn.classList.add('shake-animation');
    loginBtn.addEventListener('animationend', () => loginBtn.classList.remove('shake-animation'), { once: true });
    return;
  }
  try {
    const res = await fetch(`${API_BASE}/board/${encodeURIComponent(currentUser.id)}`);
    if (!res.ok) throw new Error('Board fetch failed');
    const data = await res.json();
    boardState = data.board;
    renderBoard();
  } catch (e) {
    console.error('fetchBoard error', e);
    showPopup('Could not fetch board (is backend running?)', 4000);
  }
}

async function fetchLeaderboardOnce() {
  try {
    const res = await fetch(`${API_BASE}/leaderboard`);
    if (!res.ok) throw new Error('Failed leaderboard fetch');
    const data = await res.json();
    renderLeaderboard(data);
  } catch (e) {
    console.error('Leaderboard load error', e);
  }
}


function startLeaderboardListener() {
  if (typeof EventSource === 'undefined') return;

  const sseUrl = '/api/leaderboard/stream';
  const es = new EventSource(sseUrl);

  es.onmessage = (e) => {
    try {
      const payload = JSON.parse(e.data);
      if (payload && payload.type === 'leaderboard_update') {
        openLeaderboardBtn.classList.add('flash-animation');
        fetchLeaderboardOnce();
      }
    } catch (err) {
      openLeaderboardBtn.classList.add('flash-animation');
      fetchLeaderboardOnce();
    }
  };

  es.onerror = (err) => {
    console.warn('Leaderboard SSE error', err);
  };

  return es;
}

// board rendering

function renderBoard() {
  if (!boardState) return;

  const frag = document.createDocumentFragment();
  boardState.squares.forEach((sq, idx) => {
    const div = document.createElement('div');
    div.className = 'cell' + (sq.marked ? ' marked' : '');
    if (highlightedBingoSquares.has(idx)) {div.classList.add('bingo');}
    div.dataset.index = idx;
    div.setAttribute('role', 'gridcell');
    div.innerText = sq.text;
    frag.appendChild(div);
  });
  boardEl.replaceChildren(frag);
}

function renderLeaderboard(data) {
  const frag = document.createDocumentFragment();
  data.forEach((entry, idx) => {
    const wrapper = document.createElement('div');
    wrapper.className = 'leaderboard-entry';

    const boxDiv = document.createElement('div');
    boxDiv.className = 'leaderboard-entry-box';
    boxDiv.setAttribute('tabindex', '0');

    const list = Array.isArray(entry.winning_squares)
      ? entry.winning_squares
      : entry.winning_squares?.list;
    
    const tooltipText = list && list.length > 0
      ? '• ' + list.join('\n• ')
      : 'No squares recorded';

    const userSpan = document.createElement('span');
    userSpan.className = 'lb-username';
    userSpan.textContent = `${idx + 1}. ${entry.username.substring(0, 20)}`;

    const timeSpan = document.createElement('span');
    timeSpan.className = 'lb-time';
    timeSpan.textContent = entry.timestamp;

    boxDiv.appendChild(userSpan);
    boxDiv.appendChild(timeSpan);
    boxDiv.setAttribute('data-tooltip', tooltipText);
    
    boxDiv.addEventListener('click', () => {
      if (list && list.length > 0) {
        const detailText = `${entry.username}'s Winning Squares:\n\n` + list.map((sq, i) => `${i + 1}. ${sq}`).join('\n');
        alert(detailText);
      }
    });
    
    wrapper.appendChild(boxDiv);
    frag.appendChild(wrapper);
  });

  leaderboardEl.replaceChildren(frag);
}

// ui
function showPopup(text, timeout = 2500) {
  popup.textContent = text;
  popup.classList.remove('hidden');
  clearTimeout(popup._timeout);
  popup._timeout = setTimeout(() => popup.classList.add('hidden'), timeout);
}

async function hashUsername(name) {
  const normalized = name.trim().toLowerCase();
  
  if (window.crypto && window.crypto.subtle) {
    try {
      const encoder = new TextEncoder();
      const data = encoder.encode(normalized);
      const digest = await crypto.subtle.digest("SHA-256", data);
      const hashArray = Array.from(new Uint8Array(digest));
      const hex = hashArray.map(b => b.toString(16).padStart(2, "0")).join("");
      return "u-" + hex.slice(0, 12);
    } catch (e) {
      console.warn('crypto not available, using fallback hash');
    }
  }
  
  let hash = 0;
  for (let i = 0; i < normalized.length; i++) {
    const char = normalized.charCodeAt(i);
    hash = ((hash << 5) - hash) + char;
    hash = hash & hash;
  }
  return "u-" + Math.abs(hash).toString(16).padStart(12, "0").slice(0, 12);
}

// event

async function onCellClick(index) {
  if (!boardState || !currentUser.id || clickLocked) return;

  clickLocked = true;

  const original = boardState.squares[index].marked;
  boardState.squares[index].marked = !original;
  renderBoard();

  try {
    const res = await fetch(`${API_BASE}/mark_square`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ user_id: currentUser.id, index })
    });
    const data = await res.json();

    if (!res.ok) {
      boardState.squares[index].marked = original;
      renderBoard();
      throw new Error(data.error || 'Mark failed');
    }
    boardState = data.board;

    if (data.bingo) {
      if (data.info === 'bingod') {
        if (!alreadyBingoPopupShown) {
          showPopup("Already Bingo'd!", 3000);
          alreadyBingoPopupShown = true;
        }
        statusEl.textContent = 'Congrats on your Bingo today!';
        newBoardBtn.style.display = 'none';
        resetBoardBtn.style.display = 'none';
      } else {
        const confirmBingo = await showBingoConfirm();
        if (confirmBingo) {
          await fetch(`${API_BASE}/submit_bingo`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
              user_id: currentUser.id,
              username: currentUser.username,
              info: data.info,
              winning_squares: data.winning_squares
            })
          });
          showPopup('BINGO!', 4000);
          statusEl.textContent = 'Bingo detected...';
          setTimeout(() => { statusEl.textContent = ''; showPopup('Congrats!'); }, 3000);
          try {highlightWinningSquares(data.winning_squares.indices);} catch (e) {}
          newBoardBtn.style.display = 'none';
          resetBoardBtn.style.display = 'none';
        } else {
          await fetch(`${API_BASE}/mark_square`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ user_id: currentUser.id, index })
          });
          showPopup('Bingo canceled.');
          boardState.squares[index].marked = original;
          renderBoard();
          statusEl.textContent = '';
        }
      }
    }
  } catch (e) {
    console.error(e);
    showPopup('Error marking square', 3000);
  } finally {
    setTimeout(() => clickLocked = false, 15);
  }
}

function highlightWinningSquares(indices) {
  if (!Array.isArray(indices) || !indices.length) return;
  indices.forEach(idx => {
    const cell = boardEl.querySelector(`.cell[data-index="${idx}"]`);
    if (cell) {
      highlightedBingoSquares.add(idx);
      cell.classList.add('bingo');
    }
  });
}

function showBingoConfirm() {
  return new Promise((resolve) => {
    bingoConfirmPopup.classList.remove('hidden');

    function cleanup() {
      bingoConfirmPopup.classList.add('hidden');
      bingoConfirmYes.removeEventListener('click', onYes);
      bingoConfirmNo.removeEventListener('click', onNo);
    }
    function onYes() {
      cleanup();
      resolve(true);
    }

    function onNo() {
      cleanup();
      resolve(false);
    }
    bingoConfirmYes.addEventListener('click', onYes);
    document.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        onYes();
      }
    });
    bingoConfirmNo.addEventListener('click', onNo);
  });
}


boardEl.addEventListener('click', (e) => {
  const cell = e.target.closest('.cell');
  if (!cell) return;
  const idx = parseInt(cell.dataset.index, 10);
  onCellClick(idx);
});

newBoardBtn.addEventListener('click', async () => {
  if (!currentUser.id) {
    loginBtn.classList.add('shake-animation');
    loginBtn.addEventListener('animationend', () => loginBtn.classList.remove('shake-animation'), { once: true });
    return;
  }
  try {
    highlightedBingoSquares = new Set();
    alreadyBingoPopupShown = false;
    const res = await fetch(`${API_BASE}/board/${encodeURIComponent(currentUser.id)}?NewBoard=true`);
    const data = await res.json();
    boardState = data.board;
    renderBoard();
    showPopup('New board generated');
  } catch (e) {
    console.error('new-board error', e);
    showPopup('Failed to create new board', 3000);
  }
});

resetBoardBtn.addEventListener('click', async () => {
  if (!currentUser.id) {
    loginBtn.classList.add('shake-animation');
    loginBtn.addEventListener('animationend', () => loginBtn.classList.remove('shake-animation'), { once: true });
    openLeaderboardBtn.classList.add('flash-animation');
    return;
  }
  try {
    highlightedBingoSquares = new Set();
    alreadyBingoPopupShown = false;
    const res = await fetch(`${API_BASE}/board/${encodeURIComponent(currentUser.id)}?reset=true`);
    const data = await res.json();
    boardState = data.board;
    renderBoard();
    showPopup('Board reset');
  } catch (e) {
    console.error('reset error', e);
    showPopup('Failed to reset', 3000);
  }
});

loginBtn.addEventListener('click', () => {
  modal.classList.remove('hidden');
  modalUsername.value = currentUser.username !== 'Guest' ? currentUser.username : '';
  modalUsername.focus();
});

modalConfirm.addEventListener('click', async () => {
  const name = modalUsername.value.trim();
  if (!name || name.length < 1) {
    showPopup('Enter a valid name', 1500);
    return;
  }
  const user_name = name.substring(0, 20);
  const user_id = await hashUsername(user_name);
  currentUser = { id: user_id, username: user_name };
  usernameEl.textContent = currentUser.username;
  modal.classList.add('hidden');
  fetchBoard();
});

modalCancel.addEventListener('click', () => modal.classList.add('hidden'));

document.addEventListener('keydown', (event) => {
  if (modal.classList.contains('hidden')) return;

  if (event.key === 'Enter') {
    modalConfirm.click();
  } else if (event.key === 'Escape') {
    modalCancel.click();
  }
});

openLeaderboardBtn.addEventListener('click', () => {
  openLeaderboardBtn.classList.remove('flash-animation');
  leaderboardPopup.classList.remove('hidden');
});

closeLeaderboardBtn.addEventListener('click', () => {
  leaderboardPopup.classList.add('hidden');
});

leaderboardPopup.addEventListener('click', (e) => {
  if (e.target === leaderboardPopup) {
    leaderboardPopup.classList.add('hidden');
  }
});


//init
(async function init() {
  usernameEl.textContent = currentUser.username;
  fetchBoard();
})();

fetchLeaderboardOnce();
setInterval(fetchLeaderboardOnce, 5000);
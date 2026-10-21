/*
 * Joyfon site.
 *
 *   The hero is a still render. Hardware renders open full size in a
 *   centred lightbox.
 */
(function () {
  'use strict';

  // ── lightbox ───────────────────────────────────────────────────────
  var box = document.getElementById('lightbox');
  var boxImg = document.getElementById('lightbox-img');
  var boxCap = document.getElementById('lightbox-cap');
  if (!box || !boxImg) return;

  var closeBtn = box.querySelector('.lightbox__close');
  var opener = null;                    // so focus goes back where it came from

  function open(img, caption, source) {
    boxImg.src = img.currentSrc || img.src;
    boxImg.alt = img.alt;
    boxCap.textContent = caption;
    box.hidden = false;
    document.body.style.overflow = 'hidden';
    opener = source;
    closeBtn.focus();
  }

  function close() {
    box.hidden = true;
    boxImg.src = '';
    document.body.style.overflow = '';
    if (opener) { opener.focus(); opener = null; }
  }

  Array.prototype.forEach.call(document.querySelectorAll('.plate'), function (plate) {
    var btn = plate.querySelector('.plate__btn');
    var img = plate.querySelector('img');
    var cap = plate.querySelector('figcaption');
    if (!btn || !img) return;

    btn.setAttribute('aria-label', 'Enlarge: ' + (cap ? cap.textContent.trim() : img.alt));
    btn.addEventListener('click', function () {
      open(img, cap ? cap.textContent.trim() : '', btn);
    });
  });

  closeBtn.addEventListener('click', close);

  // clicking the backdrop closes; clicking the image itself does not
  box.addEventListener('click', function (e) {
    if (e.target === box) close();
  });

  document.addEventListener('keydown', function (e) {
    if (box.hidden) return;
    if (e.key === 'Escape') { close(); return; }
    if (e.key === 'Tab') { e.preventDefault(); closeBtn.focus(); }
  });
})();

/** @type {import('stylelint').Config} */
export default {
  extends: ['stylelint-config-standard'],
  rules: {
    // the stylesheet predates this rule; restructuring its cascade
    // isn't worth the regression risk
    'no-descending-specificity': null,
    'no-unknown-custom-properties': true,
  },
};

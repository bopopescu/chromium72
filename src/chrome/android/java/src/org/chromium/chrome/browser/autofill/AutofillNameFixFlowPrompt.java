// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill;

import android.content.Context;
import android.support.v4.view.MarginLayoutParamsCompat;
import android.support.v4.view.ViewCompat;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.TextView.BufferType;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.modaldialog.DialogDismissalCause;
import org.chromium.chrome.browser.modaldialog.ModalDialogManager;
import org.chromium.chrome.browser.modaldialog.ModalDialogProperties;
import org.chromium.chrome.browser.modaldialog.ModalDialogView;
import org.chromium.chrome.browser.modelutil.PropertyModel;

/**
 * Prompt that asks users to confirm user's name before saving card to Google.
 */
public class AutofillNameFixFlowPrompt implements TextWatcher, ModalDialogView.Controller {
    /**
     * An interface to handle the interaction with
     * an AutofillNameFixFlowPrompt object.
     */
    public interface AutofillNameFixFlowPromptDelegate {
        /**
         * Called when dialog is dismissed.
         */
        void onPromptDismissed();

        /**
         * Called when user accepted/confirmed the prompt.
         *
         * @param name Card holder name.
         */
        void onUserAccept(String name);
    }

    private final AutofillNameFixFlowPromptDelegate mDelegate;
    private final PropertyModel mDialogModel;

    private final View mDialogView;
    private final EditText mUserNameInput;
    private final ImageView mNameFixFlowTooltipIcon;
    private PopupWindow mNameFixFlowTooltipPopup;

    private ModalDialogManager mModalDialogManager;
    private Context mContext;

    /**
     * Fix flow prompt to confirm user name before saving the card to Google.
     */
    public AutofillNameFixFlowPrompt(Context context, AutofillNameFixFlowPromptDelegate delegate,
            String title, String inferredName, String confirmButtonLabel, int drawableId) {
        mDelegate = delegate;
        LayoutInflater inflater = LayoutInflater.from(context);
        mDialogView = inflater.inflate(R.layout.autofill_name_fixflow, null);

        mUserNameInput = (EditText) mDialogView.findViewById(R.id.cc_name_edit);
        mUserNameInput.setText(inferredName, BufferType.EDITABLE);
        mNameFixFlowTooltipIcon = (ImageView) mDialogView.findViewById(R.id.cc_name_tooltip_icon);
        mNameFixFlowTooltipIcon.setOnClickListener((view) -> onTooltipIconClicked());

        mDialogModel = new PropertyModel.Builder(ModalDialogProperties.ALL_KEYS)
                               .with(ModalDialogProperties.CONTROLLER, this)
                               .with(ModalDialogProperties.TITLE, title)
                               .with(ModalDialogProperties.TITLE_ICON, context, drawableId)
                               .with(ModalDialogProperties.CUSTOM_VIEW, mDialogView)
                               .with(ModalDialogProperties.POSITIVE_BUTTON_TEXT, confirmButtonLabel)
                               .with(ModalDialogProperties.NEGATIVE_BUTTON_TEXT,
                                       context.getResources(), R.string.cancel)
                               .with(ModalDialogProperties.CANCEL_ON_TOUCH_OUTSIDE, true)
                               .build();

        // Hitting the "submit" button on the software keyboard should submit.
        mUserNameInput.setOnEditorActionListener((view, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                onClick(mDialogModel, ModalDialogView.ButtonType.POSITIVE);
                return true;
            }
            return false;
        });
    }

    /**
     * Show the dialog. If activity is null this method will not do anything.
     */
    public void show(ChromeActivity activity) {
        if (activity == null) return;

        mContext = activity;
        mModalDialogManager = activity.getModalDialogManager();
        mModalDialogManager.showDialog(mDialogModel, ModalDialogManager.ModalDialogType.APP);
        mUserNameInput.addTextChangedListener(this);
    }

    protected void dismiss(@DialogDismissalCause int dismissalCause) {
        mModalDialogManager.dismissDialog(mDialogModel, dismissalCause);
    }

    @Override
    public void afterTextChanged(Editable s) {
        mDialogModel.set(ModalDialogProperties.POSITIVE_BUTTON_DISABLED,
                mUserNameInput.getText().toString().trim().isEmpty());
    }

    @Override
    public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

    @Override
    public void onTextChanged(CharSequence s, int start, int before, int count) {}

    /**
     * Handle tooltip icon clicked. If tooltip is already opened, don't show another. Otherwise
     * create a new one.
     */
    private void onTooltipIconClicked() {
        if (mNameFixFlowTooltipPopup != null) return;

        mNameFixFlowTooltipPopup = new PopupWindow(mContext);
        int textWidth = mDialogView.getWidth() - ViewCompat.getPaddingEnd(mNameFixFlowTooltipIcon)
                - MarginLayoutParamsCompat.getMarginEnd(
                        (LinearLayout.LayoutParams) mNameFixFlowTooltipIcon.getLayoutParams());
        Runnable dismissAction = () -> {
            mNameFixFlowTooltipPopup = null;
        };
        AutofillUiUtils.showTooltip(mContext, mNameFixFlowTooltipPopup,
                R.string.autofill_save_card_prompt_cardholder_name_tooltip, textWidth,
                mUserNameInput, dismissAction);
    }

    @Override
    public void onClick(PropertyModel model, int buttonType) {
        if (buttonType == ModalDialogView.ButtonType.POSITIVE) {
            mDelegate.onUserAccept(mUserNameInput.getText().toString());
        } else if (buttonType == ModalDialogView.ButtonType.NEGATIVE) {
            mModalDialogManager.dismissDialog(model, DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
        }
    }

    @Override
    public void onDismiss(PropertyModel model, int dismissalCause) {
        mDelegate.onPromptDismissed();
    }
}

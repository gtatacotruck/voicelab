#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace vlab::ui
{
    class PongGame : public juce::Component, private juce::Timer
    {
    public:
        PongGame()
        {
            resetBall (true);
            setWantsKeyboardFocus (true);
            startTimerHz (60);
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();

            g.setColour (juce::Colour (0xff020814).withAlpha (0.92f));
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (juce::Colour (0xff2e8bff).withAlpha (0.35f));
            g.drawRoundedRectangle (b.reduced (1.0f), 10.0f, 1.5f);

            // Centre dashed line
            g.setColour (juce::Colour (0xff2e4a7a));
            const float midX = b.getCentreX();
            for (float y = 8.0f; y < b.getHeight(); y += 18.0f)
                g.fillRect (midX - 1.5f, y, 3.0f, 10.0f);

            g.setColour (juce::Colour (0xffbfe0ff));
            g.setFont (juce::Font (13.0f, juce::Font::bold));
            g.drawText (juce::String (leftScore), b.withY (10).withHeight (24).withRight ((int) midX - 20),
                        juce::Justification::centredRight);
            g.drawText (juce::String (rightScore), b.withY (10).withHeight (24).withLeft ((int) midX + 20),
                        juce::Justification::centredLeft);

            auto paddleColour = juce::Colour (0xff7fd4ff);
            g.setColour (paddleColour);
            g.fillRoundedRectangle (leftPaddle, 3.0f);
            g.fillRoundedRectangle (rightPaddle, 3.0f);

            g.setColour (juce::Colour (0xffffffff));
            g.fillEllipse (ball.getX() - ballR, ball.getY() - ballR, ballR * 2, ballR * 2);

            if (! hasMoved)
            {
                g.setColour (juce::Colour (0xff8fb8e8).withAlpha (0.8f));
                g.setFont (juce::Font (12.0f));
                g.drawText ("Move your mouse, or use Up/Down, to play",
                            b.withY (b.getBottom() - 26).withHeight (20), juce::Justification::centred);
            }
        }

        void resized() override
        {
            const float w = (float) getWidth(), h = (float) getHeight();
            leftPaddle  = { 14.0f, leftPaddle.getY(), 8.0f, 54.0f };
            rightPaddle = { w - 22.0f, rightPaddle.getY(), 8.0f, 54.0f };
            leftPaddle.setY (juce::jlimit (0.0f, h - leftPaddle.getHeight(), leftPaddle.getY()));
            rightPaddle.setY (juce::jlimit (0.0f, h - rightPaddle.getHeight(), rightPaddle.getY()));
            if (! everResized)
            {
                leftPaddle.setY (h / 2 - 27.0f);
                rightPaddle.setY (h / 2 - 27.0f);
                everResized = true;
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            hasMoved = true;
            leftPaddle.setY (juce::jlimit (0.0f, (float) getHeight() - leftPaddle.getHeight(),
                                            (float) e.position.y - leftPaddle.getHeight() / 2));
        }
        void mouseDrag (const juce::MouseEvent& e) override { mouseMove (e); }

        bool keyPressed (const juce::KeyPress& k) override
        {
            hasMoved = true;
            const float step = 26.0f;
            if (k.getKeyCode() == juce::KeyPress::upKey || k.getTextCharacter() == 'w' || k.getTextCharacter() == 'W')
                leftPaddle.setY (juce::jmax (0.0f, leftPaddle.getY() - step));
            else if (k.getKeyCode() == juce::KeyPress::downKey || k.getTextCharacter() == 's' || k.getTextCharacter() == 'S')
                leftPaddle.setY (juce::jmin ((float) getHeight() - leftPaddle.getHeight(), leftPaddle.getY() + step));
            else
                return false;
            return true;
        }

    private:
        juce::Rectangle<float> leftPaddle { 14.0f, 0.0f, 8.0f, 54.0f };
        juce::Rectangle<float> rightPaddle { 0.0f, 0.0f, 8.0f, 54.0f };
        juce::Point<float> ball;
        juce::Point<float> ballVel;
        const float ballR = 6.0f;
        int leftScore = 0, rightScore = 0;
        bool hasMoved = false;
        bool everResized = false;

        void resetBall (bool towardLeft)
        {
            ball = { getWidth() > 0 ? (float) getWidth() / 2 : 200.0f, getHeight() > 0 ? (float) getHeight() / 2 : 130.0f };
            const float speed = 3.4f;
            const float angle = juce::Random::getSystemRandom().nextFloat() * 0.7f - 0.35f;
            ballVel = { (towardLeft ? -1.0f : 1.0f) * speed * std::cos (angle), speed * std::sin (angle) };
        }

        void timerCallback() override
        {
            if (getWidth() <= 0 || getHeight() <= 0) return;

            const float aiCentre = rightPaddle.getY() + rightPaddle.getHeight() / 2;
            const float aiSpeed = 2.6f;
            if (ball.y > aiCentre + 4.0f) rightPaddle.translate (0, juce::jmin (aiSpeed, ball.y - aiCentre));
            else if (ball.y < aiCentre - 4.0f) rightPaddle.translate (0, -juce::jmin (aiSpeed, aiCentre - ball.y));
            rightPaddle.setY (juce::jlimit (0.0f, (float) getHeight() - rightPaddle.getHeight(), rightPaddle.getY()));

            ball += ballVel;

            if (ball.y - ballR < 0.0f) { ball.y = ballR; ballVel.y = -ballVel.y; }
            if (ball.y + ballR > getHeight()) { ball.y = (float) getHeight() - ballR; ballVel.y = -ballVel.y; }

            if (ballVel.x < 0 && leftPaddle.getRight() >= ball.x - ballR && ball.x - ballR > leftPaddle.getX()
                && ball.y >= leftPaddle.getY() && ball.y <= leftPaddle.getBottom())
            {
                ballVel.x = -ballVel.x * 1.03f;
                ballVel.y += (ball.y - (leftPaddle.getY() + leftPaddle.getHeight() / 2)) * 0.04f;
            }
            if (ballVel.x > 0 && rightPaddle.getX() <= ball.x + ballR && ball.x + ballR < rightPaddle.getRight()
                && ball.y >= rightPaddle.getY() && ball.y <= rightPaddle.getBottom())
            {
                ballVel.x = -ballVel.x * 1.03f;
                ballVel.y += (ball.y - (rightPaddle.getY() + rightPaddle.getHeight() / 2)) * 0.04f;
            }

            if (ball.x < -20.0f)      { ++rightScore; resetBall (false); }
            else if (ball.x > getWidth() + 20.0f) { ++leftScore;  resetBall (true); }

            repaint();
        }
    };
}
